#pragma once

#include "ai/Action.hpp"
#include "ai/CommandProcessor.hpp"
#include "voice/AudioCapture.hpp"
#include "voice/WhisperEngine.hpp"

#include <atomic>
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

/**
 * VAD-driven listen loop: capture → Whisper → CommandProcessor → optional Piper reply.
 */
class VoiceListener {
public:
    VoiceListener(WhisperEngine& whisper,
                  AudioCapture& capture,
                  CommandProcessor& commands,
                  std::atomic<bool>& app_running,
                  std::string piper_model_path,
                  VoiceListenerConfig cfg = {});

    void run();

private:
    bool voiceActive(const std::vector<float>& samples) const;
    static float rms(const std::vector<float>& samples, size_t start, size_t end);
    static std::string sanitizeForTts(std::string s);
    void speakReply(const Action& action);

    WhisperEngine& whisper_;
    AudioCapture& capture_;
    CommandProcessor& commands_;
    std::atomic<bool>& app_running_;
    std::string piper_model_path_;
    VoiceListenerConfig cfg_;
};
