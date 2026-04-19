#include "voice/VoiceListener.hpp"

#include "tts/PiperSpeech.hpp"

#include <cmath>
#include <future>
#include <iostream>
#include <regex>
#include <thread>

VoiceListener::VoiceListener(WhisperEngine& whisper,
                             AudioCapture& capture,
                             CommandProcessor& commands,
                             std::atomic<bool>& app_running,
                             std::string piper_model_path,
                             VoiceListenerConfig cfg)
    : whisper_(whisper),
      capture_(capture),
      commands_(commands),
      app_running_(app_running),
      piper_model_path_(std::move(piper_model_path)),
      cfg_(cfg) {}

float VoiceListener::rms(const std::vector<float>& samples, size_t start, size_t end) {
    if (start >= end || end > samples.size()) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (size_t i = start; i < end; ++i) {
        sum += samples[i] * samples[i];
    }
    return std::sqrt(sum / static_cast<float>(end - start));
}

bool VoiceListener::voiceActive(const std::vector<float>& samples) const {
    if (samples.size() < static_cast<size_t>(cfg_.vad_window_samples)) {
        return false;
    }
    const size_t start = samples.size() - static_cast<size_t>(cfg_.vad_window_samples);
    return rms(samples, start, samples.size()) > cfg_.voice_rms_threshold;
}

std::string VoiceListener::sanitizeForTts(std::string s) {
    s = std::regex_replace(s, std::regex(R"(\*{1,3})"), "");
    s = std::regex_replace(s, std::regex(R"(^#{1,6}\s+)", std::regex_constants::multiline), "");
    s = std::regex_replace(s, std::regex(R"(^[\-\*]\s+)", std::regex_constants::multiline), "");
    s = std::regex_replace(s, std::regex(R"(^\d+\.\s+)", std::regex_constants::multiline), "");
    s = std::regex_replace(s, std::regex(R"(`([^`]+)`)"), "$1");
    s = std::regex_replace(s, std::regex(R"([\r\n\t]+)"), " ");
    s = std::regex_replace(s, std::regex(R"( {2,})"), " ");

    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

void VoiceListener::speakReply(const Action& action) {
    std::cout << "🤖 Route: " << actionKindLabel(action.kind)
              << "  mode=" << (mode_ == ListenerMode::Lesson ? "lesson" : "assistant") << std::endl;
    if (action.reply.empty()) {
        return;
    }

    capture_.pauseDevice();
    capture_.clearBuffer();

    if (!piper_speak_and_play(sanitizeForTts(action.reply), piper_model_path_)) {
        std::cerr << "🔇 TTS failed; reply text: " << action.reply << std::endl;
    }

    capture_.clearBuffer();
    capture_.resumeDevice();
}

Action VoiceListener::routeTranscript(const std::string& transcript) {
    // Always give the fast local matcher a chance first — this is what lets the user
    // switch modes by voice ("start english lesson" / "exit lesson") from either side.
    if (auto local = commands_.match_local(transcript)) {
        if (local->kind == ActionKind::LessonModeToggle) {
            static const std::regex re_start(
                R"(\b(start|begin|open)\s+(english\s+)?lesson\b)",
                std::regex_constants::ECMAScript | std::regex_constants::icase);
            mode_ = std::regex_search(transcript, re_start)
                        ? ListenerMode::Lesson
                        : ListenerMode::Assistant;
        }
        return *local;
    }

    if (mode_ == ListenerMode::Lesson && tutor_cb_) {
        return tutor_cb_(transcript);
    }
    // Fall back to the external API (handled by the full `process` pipeline).
    return commands_.process(transcript);
}

void VoiceListener::run() {
    int silence_ms = 0;
    bool collecting = false;
    int speech_ms = 0;

    std::cout << "\n🎤 Đang lắng nghe... (Nói bất kỳ lúc nào!)" << std::endl;
    std::cout << "Nhấn Ctrl+C để thoát.\n" << std::endl;

    capture_.resumeDevice();

    while (app_running_.load()) {
        const std::vector<float> live = capture_.snapshotBuffer();
        const bool has_voice = voiceActive(live);

        if (has_voice && !collecting) {
            collecting = true;
            speech_ms = 0;
            silence_ms = 0;
            std::cout << "🔴 Đang ghi âm..." << std::endl;
        }

        if (collecting) {
            speech_ms += cfg_.poll_interval_ms;

            if (has_voice) {
                silence_ms = 0;
            } else {
                silence_ms += cfg_.poll_interval_ms;
            }

            if (speech_ms >= cfg_.min_speech_ms && silence_ms >= cfg_.end_silence_ms) {
                collecting = false;
                std::cout << "⏹ Ghi âm hoàn thành!" << std::endl;

                const std::string transcript = whisper_.transcribe(live);
                if (!transcript.empty()) {
                    std::future<Action> fut = std::async(std::launch::async,
                        [this, transcript]() { return routeTranscript(transcript); });
                    speakReply(fut.get());
                    std::cout << std::endl;
                }

                capture_.clearBuffer();
                std::cout << "🎤 Lắng nghe tiếp...\n" << std::endl;
            }
        }

        capture_.limitBufferSize(cfg_.buffer_max_seconds, cfg_.buffer_keep_seconds);
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.poll_interval_ms));
    }

    capture_.pauseDevice();
}
