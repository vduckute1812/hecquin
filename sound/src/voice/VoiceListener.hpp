#pragma once

#include "actions/Action.hpp"
#include "ai/CommandProcessor.hpp"
#include "voice/AudioCapture.hpp"
#include "voice/ListenerMode.hpp"
#include "voice/MusicSideEffects.hpp"
#include "voice/SecondaryVadGate.hpp"
#include "voice/WhisperEngine.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct VoiceListenerConfig {
    int vad_window_samples = 512;
    int min_speech_ms = 500;
    int end_silence_ms = 800;
    float voice_rms_threshold = 0.02f;
    int poll_interval_ms = 50;
    int buffer_max_seconds = 30;
    int buffer_keep_seconds = 10;
    // Fraction of poll frames during collection that must register as voiced
    // (denominator excludes the trailing end-silence tail so short
    // utterances aren't unfairly punished).  Low-energy rustling or brief
    // music spikes rarely exceed ~20 %; actual speech is typically 60–95 %.
    // Set to 0 to disable.
    float min_voiced_frame_ratio = 0.30f;
    // Minimum mean-RMS over the whole collected utterance.  Rejects whispers
    // and faint background chatter that briefly crossed the VAD threshold.
    float min_utterance_rms = 0.015f;

    // ------------------------------------------------------------------
    // Adaptive VAD (auto-tune to the user's mic / room)
    // ------------------------------------------------------------------
    /**
     * When true (default), `UtteranceCollector` measures the ambient
     * noise floor at startup and derives the start / continue / utterance
     * thresholds as multiples of it.  Set to false (or `HECQUIN_VAD_AUTO=0`)
     * to use the static `voice_rms_threshold` / `min_utterance_rms`.
     */
    bool auto_calibrate = true;
    /**
     * When true, the noise floor keeps tracking idle (non-collecting)
     * frames after calibration so the gate self-corrects when the room
     * gets noisier or quieter.  Disable to freeze thresholds after the
     * initial calibration window.
     */
    bool auto_adapt = true;
    /** start_threshold = clamp(k_start * noise_floor, ...). */
    float k_start = 3.0f;
    /** continue_threshold = k_continue * start_threshold (hysteresis). */
    float k_continue = 0.6f;
    /** min_utterance_rms = clamp(k_utt * noise_floor, ...). */
    float k_utt = 2.0f;
    /** Calibration window length; truncated to >= 1 poll frame internally. */
    int calibration_ms = 1000;
    /** EMA smoothing factor for runtime idle-frame updates (0..1). */
    float ema_alpha = 0.05f;
    /**
     * Hard clamps so derived thresholds never wander outside sane bounds.
     *
     * `adaptive_min_start_thr` is the dominant knob in quiet rooms: when
     * the calibrated floor is tiny the start threshold clamps up to this
     * value, and the hysteresis continue threshold becomes
     * `k_continue * adaptive_min_start_thr`.  At 0.005 the resulting
     * continue threshold (~0.003 with default `k_continue = 0.6`) sits
     * comfortably above the ambient peaks typical of a quiet desk
     * (~0.001-0.0025 RMS) so the silence timer can fire reliably.
     */
    float adaptive_min_start_thr = 0.005f;
    float adaptive_max_start_thr = 0.10f;
    float adaptive_min_utt_rms = 0.002f;
    float adaptive_max_utt_rms = 0.08f;
    /**
     * Defense-in-depth cap on a single utterance.  When `frame_rms`
     * stays above the continue threshold indefinitely (e.g. a noisy
     * environment that the noise floor hasn't caught up with, or
     * persistent music on the channel) the collector force-closes
     * after this many milliseconds and lets the secondary gate
     * decide.  Set to 0 to disable the safety net.
     */
    int max_utterance_ms = 15000;
    /** When true, the collector logs the live noise floor + thresholds. */
    bool debug = false;

    /**
     * Set by `apply_env_overrides()` when the user pinned a specific
     * threshold via `HECQUIN_VAD_VOICE_RMS_THRESHOLD` /
     * `HECQUIN_VAD_MIN_UTTERANCE_RMS`.  Pinned fields bypass the
     * adaptive logic — other fields keep auto-tuning.
     */
    bool voice_rms_threshold_pinned = false;
    bool min_utterance_rms_pinned = false;

    /**
     * Override thresholds from `HECQUIN_VAD_*` env vars so users can tune the
     * gate for their mic / room without rebuilding:
     *   HECQUIN_VAD_VOICE_RMS_THRESHOLD   pin start threshold (default 0.02)
     *   HECQUIN_VAD_MIN_VOICED_RATIO      (default 0.30, 0 disables)
     *   HECQUIN_VAD_MIN_UTTERANCE_RMS     pin secondary-gate min RMS (default 0.015, 0 disables)
     *   HECQUIN_VAD_AUTO                  "0" / "false" disables auto-tuning entirely
     *   HECQUIN_VAD_K_START               start_threshold multiplier (default 3.0)
     *   HECQUIN_VAD_K_CONTINUE            hysteresis multiplier (default 0.6)
     *   HECQUIN_VAD_K_UTT                 min-utterance-rms multiplier (default 2.0)
     *   HECQUIN_VAD_MIN_START_THR         lower clamp on adaptive start threshold (default 0.005)
     *   HECQUIN_VAD_MAX_UTTERANCE_MS      hard cap on one recording (default 15000, 0 disables)
     *   HECQUIN_VAD_DEBUG                 "1" prints live floor + thresholds
     */
    void apply_env_overrides();
};

/**
 * One captured utterance handed to a lesson- / drill-mode callback.
 *
 * The raw PCM buffer is kept alongside the transcript because pronunciation
 * and intonation scoring need the acoustic frames, not just the words.  The
 * buffer stays mono float32 at `AudioCapture`'s configured sample rate
 * (16 kHz by default) so downstream modules can feed wav2vec2 / YIN directly.
 */
struct Utterance {
    std::string transcript;
    std::vector<float> pcm_16k;
};

using TutorCallback = std::function<Action(const Utterance&)>;

/** Callback that provides an opening prompt whenever the listener enters drill mode. */
using DrillAnnounceCallback = std::function<void()>;

/**
 * Callback invoked when an utterance arrives while the listener is in
 * `ListenerMode::Music`.  Receives just the transcript (music playback
 * does not need the PCM buffer) and returns a `MusicPlayback` action.
 */
using MusicCallback = std::function<Action(const std::string& query)>;

/**
 * Side-effect callbacks the listener fires when the user issues a
 * mid-song control intent.  Each is a bare `void()` — the listener
 * already has the parsed `Action` and does the speech reply itself,
 * so the music layer just needs to do the audio thing.  All three
 * are optional; absent callbacks are silently no-ops, which keeps
 * binaries that don't link a music provider compiling without
 * stubs.
 */
using MusicAbortCallback  = std::function<void()>;
using MusicPauseCallback  = std::function<void()>;
using MusicResumeCallback = std::function<void()>;

/**
 * Sink for one internal pipeline event — wired to
 * `LearningStore::record_pipeline_event` when a store is attached.  Keeping
 * it as a `std::function` lets the voice layer stay free of any
 * `learning/` dependency, mirroring `ApiCallSink`.
 */
struct PipelineEvent {
    std::string event;      ///< "vad_gate" | "whisper" | "tts" | "drill" | …
    std::string outcome;    ///< "ok" | "skipped" | "error"
    long duration_ms = 0;
    std::string attrs_json; ///< optional JSON attrs; empty = {}
};
using PipelineEventSink = std::function<void(const PipelineEvent&)>;

namespace hecquin::voice {
class UtteranceCollector;
class UtteranceRouter;
class TtsResponsePlayer;
class PipelineTelemetry;
struct CollectedUtterance;
struct VadGateDecision;
} // namespace hecquin::voice

/**
 * VAD-driven listen loop: capture → Whisper → CommandProcessor / tutor callback → Piper reply.
 *
 * The listener now coordinates four collaborators that each own a
 * single concern (see `src/voice/`):
 *
 *   - `UtteranceCollector`  — poll loop + primary VAD
 *   - `SecondaryVadGate`    — post-collection accept/reject decision
 *   - `UtteranceRouter`     — chain of responsibility for dispatch
 *   - `TtsResponsePlayer`   — sanitiser + mic-mute + streaming playback
 *
 * In `Assistant` mode transcripts flow through `CommandProcessor`. In `Lesson` and
 * `Drill` modes they flow through the attached tutor callback (if any).  Voice
 * commands `start/exit english lesson` and `start/exit pronunciation drill`
 * flip the mode via `LessonModeToggle` / `DrillModeToggle` actions.
 */
class VoiceListener {
public:
    VoiceListener(WhisperEngine& whisper,
                  AudioCapture& capture,
                  CommandProcessor& commands,
                  std::atomic<bool>& app_running,
                  std::string piper_model_path,
                  VoiceListenerConfig cfg = {});
    ~VoiceListener();

    /** Install a lesson/drill-mode handler. Without one, lesson/drill mode falls back to Assistant. */
    void setTutorCallback(TutorCallback cb) { tutor_cb_ = std::move(cb); }

    /** Install a drill-mode handler (separate from lesson so processors can coexist). */
    void setDrillCallback(TutorCallback cb) { drill_cb_ = std::move(cb); }

    /** Optional hook invoked when the listener enters drill mode (announce the first sentence). */
    void setDrillAnnounceCallback(DrillAnnounceCallback cb) { drill_announce_cb_ = std::move(cb); }

    /**
     * Install a music-session handler.  Without one, `open music` still
     * acknowledges (MusicSearchPrompt reply is spoken) but the follow-up
     * song query falls through to the chat fallback.
     */
    void setMusicCallback(MusicCallback cb) { music_cb_ = std::move(cb); }

    /**
     * Install side-effect callbacks fired on mid-song control intents
     * (`stop / pause / continue music`).  Wired to
     * `MusicSession::abort / pause / resume` in `voice_detector` /
     * `english_tutor`; binaries that don't surface music can leave them
     * unset.
     */
    void setMusicAbortCallback(MusicAbortCallback cb)   { music_fx_.set_abort_callback(std::move(cb)); }
    void setMusicPauseCallback(MusicPauseCallback cb)   { music_fx_.set_pause_callback(std::move(cb)); }
    void setMusicResumeCallback(MusicResumeCallback cb) { music_fx_.set_resume_callback(std::move(cb)); }

    /**
     * Install (or clear) the per-event telemetry sink.
     *
     * Stored both directly and inside the internal `PipelineTelemetry`
     * collaborator so legacy callers that still poke `event_sink_`
     * (none today, kept for forward-compat with the existing public
     * surface) and the typed emit_* helpers stay in sync.
     */
    void setPipelineEventSink(PipelineEventSink s);

    /**
     * Verdict from the secondary VAD gate that runs after the end-silence
     * timer fires.  `accept` is true iff the utterance should be handed to
     * Whisper.  Otherwise `too_quiet` and/or `too_sparse` flags carry the
     * precise reason(s) — both may be set if the audio missed on both knobs.
     */
    struct VadGateDecision {
        bool accept;
        bool too_quiet;
        bool too_sparse;
        float mean_rms;
        float voiced_ratio;
    };

    /**
     * Pure function form of the secondary gate that `run()` applies once an
     * utterance has been collected.  Exposed as `static` so tests can drive
     * it without needing real audio capture + Whisper infrastructure.
     *
     * `effective_frames` = total polled frames minus the tail silence that
     * triggered the end-of-utterance transition (so short utterances aren't
     * unfairly penalised).  Must be ≥ 1.
     */
    [[nodiscard]] static VadGateDecision evaluate_secondary_gate(
        int voiced_frames, int effective_frames, float mean_rms,
        const VoiceListenerConfig& cfg);

    /** Force-start in a specific mode (used by the dedicated english_tutor / pronunciation_drill binaries). */
    void setInitialMode(ListenerMode mode) { mode_ = mode; }

    /**
     * Mode to return to when the user says "exit drill" / "exit lesson".
     * Defaults to `Assistant`.  `english_tutor` sets it to `Lesson` so that
     * exiting a temporary drill session drops back into lessons (not into
     * the generic chat fallback).  If the user tries to exit their own
     * home mode, the listener falls back to Assistant as an escape hatch.
     */
    void setHomeMode(ListenerMode mode) { home_mode_ = mode; }

    void run();

private:
    // ---- Loop phases (see `run()` for the orchestration order) -------
    /** Print the one-line "Auto-VAD on/off" startup banner. */
    void print_startup_banner_() const;
    /**
     * Apply secondary gate → transcribe → route → speak for one
     * collected utterance.  Returns silently when the gate rejects;
     * `run()` does not need to differentiate the outcome.
     */
    void process_utterance_(hecquin::voice::CollectedUtterance& utt,
                            hecquin::voice::UtteranceRouter& router);
    /**
     * Run the secondary VAD gate; on reject, log + emit telemetry +
     * clean the capture buffer and pause briefly.  Returns true when
     * the utterance should proceed to Whisper.
     */
    bool gate_accepts_(const hecquin::voice::CollectedUtterance& utt);
    /**
     * Run Whisper + emit the `whisper` telemetry event; returns the
     * (possibly empty) transcript.  Empty result means Whisper itself
     * filtered the audio out and the caller should skip routing.
     */
    std::string transcribe_and_emit_(const hecquin::voice::CollectedUtterance& utt);
    /** Apply mode side effects + speak the reply. */
    void handle_routed_(const Action& action);
    /** After a drill score, announce the next sentence using a MuteGuard. */
    void maybe_announce_drill_(ActionKind action_kind);

    void apply_local_intent_side_effects_(const Action& local);
    void log_vad_rejection_(const hecquin::voice::VadGateDecision& gate) const;
    const char* current_mode_label_() const;

    WhisperEngine& whisper_;
    AudioCapture& capture_;
    CommandProcessor& commands_;
    TutorCallback tutor_cb_;
    TutorCallback drill_cb_;
    DrillAnnounceCallback drill_announce_cb_;
    MusicCallback music_cb_;
    hecquin::voice::MusicSideEffects music_fx_;
    PipelineEventSink event_sink_;
    std::atomic<bool>& app_running_;
    std::string piper_model_path_;
    VoiceListenerConfig cfg_;
    ListenerMode mode_ = ListenerMode::Assistant;
    ListenerMode home_mode_ = ListenerMode::Assistant;
    bool pending_drill_announce_ = false;

    std::unique_ptr<hecquin::voice::UtteranceCollector> collector_;
    std::unique_ptr<hecquin::voice::TtsResponsePlayer> player_;
    std::unique_ptr<hecquin::voice::PipelineTelemetry> telemetry_;
};
