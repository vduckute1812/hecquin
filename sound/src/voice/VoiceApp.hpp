#pragma once

#include "config/AppConfig.hpp"
#include "voice/AudioCapture.hpp"
#include "voice/WhisperEngine.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace hecquin::voice {

/**
 * Shared bootstrap for voice-pipeline CLIs.
 *
 * Handles the identical 20-line preamble (SIGINT handler, whisper load,
 * `AppConfig::load`, SDL capture open) that `voice_detector` and
 * `english_tutor` both need, and owns the process-lifetime resources so
 * callers can focus on wiring their specific processor / tutor callback.
 *
 * Usage:
 *   VoiceApp app({ .model_path = …, .config_path = …, });
 *   if (!app.init()) return 1;
 *   VoiceListener listener(app.whisper(), app.capture(), …);
 *   listener.run();
 *   app.shutdown();  // optional: dtor also calls this
 */
class VoiceApp {
public:
    struct Options {
        std::string model_path;          // whisper.cpp ggml model
        std::string piper_model_path;    // Piper TTS onnx (used by caller)
        std::string config_path;         // .env-style config
        std::string prompts_dir;         // optional; empty = none
    };

    explicit VoiceApp(Options opts);
    ~VoiceApp();

    VoiceApp(const VoiceApp&) = delete;
    VoiceApp& operator=(const VoiceApp&) = delete;

    /**
     * Install SIGINT handler, load whisper, parse config, open the mic.
     * Prints a human-readable error if any step fails and returns `false`.
     */
    bool init();

    /** Close the audio device and call SDL_Quit; idempotent. */
    void shutdown();

    WhisperEngine&       whisper()       { return *whisper_; }
    AudioCapture&        capture()       { return capture_; }
    AppConfig&           config()        { return config_; }
    const Options&       options() const { return options_; }
    std::atomic<bool>&   running();

    const std::string& piper_model_path() const { return options_.piper_model_path; }

private:
    Options                        options_;
    AppConfig                      config_{};
    std::unique_ptr<WhisperEngine> whisper_;
    AudioCapture                   capture_{};
    bool                           audio_opened_ = false;
    bool                           shut_down_    = false;
};

} // namespace hecquin::voice
