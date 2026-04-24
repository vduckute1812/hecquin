#pragma once

#include "actions/Action.hpp"
#include "ai/CommandProcessor.hpp"
#include "voice/AudioCapture.hpp"
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

    /**
     * Override thresholds from `HECQUIN_VAD_*` env vars so users can tune the
     * gate for their mic / room without rebuilding:
     *   HECQUIN_VAD_VOICE_RMS_THRESHOLD   (default 0.02)
     *   HECQUIN_VAD_MIN_VOICED_RATIO      (default 0.30, 0 disables)
     *   HECQUIN_VAD_MIN_UTTERANCE_RMS     (default 0.015, 0 disables)
     */
    void apply_env_overrides();
};

enum class ListenerMode {
    Assistant,
    Lesson,
    Drill,
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

    /** Optional per-event telemetry sink. */
    void setPipelineEventSink(PipelineEventSink s) { event_sink_ = std::move(s); }

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
    void apply_local_intent_side_effects_(const Action& local);
    void handle_vad_rejection_(const hecquin::voice::VadGateDecision& gate,
                               int speech_ms);
    const char* current_mode_label_() const;

    WhisperEngine& whisper_;
    AudioCapture& capture_;
    CommandProcessor& commands_;
    TutorCallback tutor_cb_;
    TutorCallback drill_cb_;
    DrillAnnounceCallback drill_announce_cb_;
    PipelineEventSink event_sink_;
    std::atomic<bool>& app_running_;
    std::string piper_model_path_;
    VoiceListenerConfig cfg_;
    ListenerMode mode_ = ListenerMode::Assistant;
    ListenerMode home_mode_ = ListenerMode::Assistant;
    bool pending_drill_announce_ = false;

    std::unique_ptr<hecquin::voice::UtteranceCollector> collector_;
    std::unique_ptr<hecquin::voice::TtsResponsePlayer> player_;
};
