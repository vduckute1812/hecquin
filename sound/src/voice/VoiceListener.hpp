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
};

enum class ListenerMode {
    Assistant,
    Lesson,
};

using TutorCallback = std::function<Action(const std::string&)>;

/**
 * VAD-driven listen loop: capture → Whisper → CommandProcessor / tutor callback → Piper reply.
 *
 * In `Assistant` mode transcripts flow through `CommandProcessor`. In `Lesson` mode they
 * flow through the attached tutor callback (if any). Voice commands
 * "start/exit english lesson" flip the mode via a `LessonModeToggle` action.
 */
class VoiceListener {
public:
    VoiceListener(WhisperEngine& whisper,
                  AudioCapture& capture,
                  CommandProcessor& commands,
                  std::atomic<bool>& app_running,
                  std::string piper_model_path,
                  VoiceListenerConfig cfg = {});

    /** Install a lesson-mode handler. Optional — without one, lesson mode falls back to Assistant. */
    void set_tutor_callback(TutorCallback cb) { tutor_cb_ = std::move(cb); }

    /** Force-start in lesson mode (used by the dedicated english_tutor binary). */
    void set_initial_mode(ListenerMode mode) { mode_ = mode; }

    void run();

private:
    bool voiceActive(const std::vector<float>& samples) const;
    static float rms(const std::vector<float>& samples, size_t start, size_t end);
    static std::string sanitizeForTts(std::string s);
    void speakReply(const Action& action);
    Action routeTranscript_(const std::string& transcript);

    WhisperEngine& whisper_;
    AudioCapture& capture_;
    CommandProcessor& commands_;
    TutorCallback tutor_cb_;
    std::atomic<bool>& app_running_;
    std::string piper_model_path_;
    VoiceListenerConfig cfg_;
    ListenerMode mode_ = ListenerMode::Assistant;
};
