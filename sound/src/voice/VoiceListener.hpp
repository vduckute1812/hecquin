#pragma once

#include "actions/Action.hpp"
#include "ai/CommandProcessor.hpp"
#include "voice/AudioCapture.hpp"
#include "voice/WhisperEngine.hpp"

#include <atomic>
#include <functional>
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
    // Fraction of poll frames during collection that must register as voiced.
    // Low-energy rustling or brief music spikes rarely exceed ~20 %, while
    // actual speech is typically 50–90 %.  Set to 0 to disable.
    float min_voiced_frame_ratio = 0.35f;
    // Minimum mean-RMS over the whole collected utterance.  Rejects whispers
    // and faint background chatter that briefly crossed the VAD threshold.
    float min_utterance_rms = 0.015f;
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
 * VAD-driven listen loop: capture → Whisper → CommandProcessor / tutor callback → Piper reply.
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

    /** Install a lesson/drill-mode handler. Without one, lesson/drill mode falls back to Assistant. */
    void setTutorCallback(TutorCallback cb) { tutor_cb_ = std::move(cb); }

    /** Install a drill-mode handler (separate from lesson so processors can coexist). */
    void setDrillCallback(TutorCallback cb) { drill_cb_ = std::move(cb); }

    /** Optional hook invoked when the listener enters drill mode (announce the first sentence). */
    void setDrillAnnounceCallback(DrillAnnounceCallback cb) { drill_announce_cb_ = std::move(cb); }

    /** Force-start in a specific mode (used by the dedicated english_tutor / pronunciation_drill binaries). */
    void setInitialMode(ListenerMode mode) { mode_ = mode; }

    void run();

private:
    bool voiceActive(const std::vector<float>& samples) const;
    static float rms(const std::vector<float>& samples, size_t start, size_t end);
    static std::string sanitizeForTts(std::string s);
    void speakReply(const Action& action);
    Action routeUtterance(const Utterance& utterance);

    WhisperEngine& whisper_;
    AudioCapture& capture_;
    CommandProcessor& commands_;
    TutorCallback tutor_cb_;
    TutorCallback drill_cb_;
    DrillAnnounceCallback drill_announce_cb_;
    std::atomic<bool>& app_running_;
    std::string piper_model_path_;
    VoiceListenerConfig cfg_;
    ListenerMode mode_ = ListenerMode::Assistant;
    bool pending_drill_announce_ = false;
};
