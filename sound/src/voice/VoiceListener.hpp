#pragma once

#include "actions/Action.hpp"
#include "ai/CommandProcessor.hpp"
#include "voice/AudioBargeInController.hpp"
#include "voice/AudioCapture.hpp"
#include "voice/ListenerMode.hpp"
#include "voice/MusicSideEffects.hpp"
#include "voice/PipelineEvent.hpp"
#include "voice/SecondaryVadGate.hpp"
#include "voice/VoiceListenerConfig.hpp"
#include "voice/WhisperEngine.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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

// PipelineEvent / PipelineEventSink moved to voice/PipelineEvent.hpp
// (re-exported via the include above).

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

    /**
     * Access the barge-in controller so wiring code (e.g. `MusicWiring`,
     * `TtsResponsePlayer`) can install gain / abort sinks and toggle
     * `set_music_active` / `set_tts_active` from outside.  Returns a
     * mutable reference because every collaborator that touches it
     * mutates state.
     */
    hecquin::voice::AudioBargeInController& barge_in() { return barge_; }

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
    /**
     * Barge-in coordinator.  Default-constructed with built-in
     * defaults; `apply_env_overrides` runs in the constructor so the
     * controller picks up `HECQUIN_DUCK_*` / `HECQUIN_TTS_*` knobs
     * even when callers reuse the no-arg `VoiceListenerConfig`.
     */
    hecquin::voice::AudioBargeInController barge_;
};
