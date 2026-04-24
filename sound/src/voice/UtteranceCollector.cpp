#include "voice/UtteranceCollector.hpp"

#include "voice/AudioCapture.hpp"
#include "voice/VoiceListener.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace hecquin::voice {

UtteranceCollector::UtteranceCollector(AudioCapture& capture,
                                       const VoiceListenerConfig& cfg,
                                       const std::atomic<bool>& app_running)
    : capture_(capture), cfg_(cfg), app_running_(app_running) {}

float UtteranceCollector::rms(const std::vector<float>& samples,
                              std::size_t start, std::size_t end) {
    if (start >= end || end > samples.size()) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (std::size_t i = start; i < end; ++i) {
        sum += samples[i] * samples[i];
    }
    return std::sqrt(sum / static_cast<float>(end - start));
}

bool UtteranceCollector::voice_active_(const std::vector<float>& samples) const {
    if (samples.size() < static_cast<std::size_t>(cfg_.vad_window_samples)) {
        return false;
    }
    const std::size_t start = samples.size() - static_cast<std::size_t>(cfg_.vad_window_samples);
    return rms(samples, start, samples.size()) > cfg_.voice_rms_threshold;
}

std::optional<CollectedUtterance> UtteranceCollector::collect_next() {
    CollectedUtterance u;
    bool collecting = false;
    int silence_ms = 0;

    std::vector<float> vad_window;
    vad_window.reserve(static_cast<std::size_t>(cfg_.vad_window_samples));

    while (app_running_.load()) {
        capture_.snapshotRecent(static_cast<std::size_t>(cfg_.vad_window_samples),
                                vad_window);
        const bool has_voice = voice_active_(vad_window);

        if (has_voice && !collecting) {
            collecting = true;
            u = {};
            silence_ms = 0;
            std::cout << "🔴 Recording..." << std::endl;
        }

        if (collecting) {
            u.speech_ms += cfg_.poll_interval_ms;
            ++u.total_frames;
            if (has_voice) {
                ++u.voiced_frames;
                silence_ms = 0;
            } else {
                silence_ms += cfg_.poll_interval_ms;
            }

            if (u.speech_ms >= cfg_.min_speech_ms && silence_ms >= cfg_.end_silence_ms) {
                u.silence_ms = silence_ms;
                std::cout << "⏹ Recording complete!" << std::endl;
                // Only the close-of-utterance path needs the full buffer.
                u.pcm = capture_.snapshotBuffer();
                return u;
            }
        }

        capture_.limitBufferSize(cfg_.buffer_max_seconds, cfg_.buffer_keep_seconds);
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.poll_interval_ms));
    }

    return std::nullopt;
}

} // namespace hecquin::voice
