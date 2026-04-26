#include "voice/VoiceApp.hpp"

#include "tts/PiperSpeech.hpp"
#include "voice/CapabilityReport.hpp"

#include <SDL.h>

#include <atomic>
#include <csignal>
#include <iostream>

namespace hecquin::voice {

namespace {

/**
 * Shared running flag.  Two copies of main() used to own a static — this
 * lives here so the signal handler and VoiceApp agree without coupling via
 * extern globals.
 */
std::atomic<bool> g_app_running{true};

void handle_sigint(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n[VoiceApp] Received SIGINT; shutting down..." << std::endl;
        g_app_running = false;
    }
}

} // namespace

std::atomic<bool>& VoiceApp::running() { return g_app_running; }

VoiceApp::VoiceApp(Options opts) : options_(std::move(opts)) {}

VoiceApp::~VoiceApp() { shutdown(); }

bool VoiceApp::init() {
    std::signal(SIGINT, &handle_sigint);
    g_app_running = true;

    // Load AppConfig first so we can feed locale.whisper_language into
    // WhisperConfig before constructing the engine.  Env overrides still
    // win over the file-loaded defaults (via `apply_env_overrides`).
    const char* prompts_dir =
        options_.prompts_dir.empty() ? nullptr : options_.prompts_dir.c_str();
    config_ = AppConfig::load(options_.config_path.c_str(), prompts_dir);

    WhisperConfig wcfg;
    if (!config_.locale.whisper_language.empty()) {
        wcfg.language = config_.locale.whisper_language;
    }
    wcfg.apply_env_overrides();
    whisper_ = std::make_unique<WhisperEngine>(options_.model_path.c_str(), wcfg);
    if (!whisper_->isLoaded()) {
        std::cerr << "[VoiceApp] Failed to load whisper model at "
                  << options_.model_path << std::endl;
        return false;
    }

    if (!capture_.open(g_app_running, config_.audio)) {
        std::cerr << "[VoiceApp] Failed to open audio capture device "
                  << config_.audio.device_index << std::endl;
        return false;
    }
    audio_opened_ = true;
    return true;
}

void VoiceApp::speak_capability_summary() {
    const auto status = probe_capabilities(config_);
    const std::string summary = status.spoken_summary();
    if (summary.empty()) return;
    std::cout << "[VoiceApp] capability summary: " << summary << std::endl;
    if (options_.piper_model_path.empty()) return;
    // Best-effort: failures land in stderr but never abort startup —
    // the summary is informational, not load-bearing.
    (void)piper_speak_and_play(summary, options_.piper_model_path);
}

void VoiceApp::shutdown() {
    if (shut_down_) return;
    shut_down_ = true;
    if (audio_opened_) {
        capture_.close();
        audio_opened_ = false;
    }
    SDL_Quit();
}

} // namespace hecquin::voice
