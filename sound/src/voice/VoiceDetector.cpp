#include "ai/CommandProcessor.hpp"
#include "config/AppConfig.hpp"
#include "voice/AudioCapture.hpp"
#include "voice/VoiceListener.hpp"
#include "voice/WhisperEngine.hpp"

#include <SDL.h>

#include <atomic>
#include <csignal>
#include <iostream>

#ifndef DEFAULT_MODEL_PATH
#define DEFAULT_MODEL_PATH ".env/models/ggml-base.bin"
#endif

#ifndef DEFAULT_PIPER_MODEL_PATH
#define DEFAULT_PIPER_MODEL_PATH ".env/shared/models/piper/en_US-lessac-medium.onnx"
#endif

#ifndef DEFAULT_CONFIG_PATH
#define DEFAULT_CONFIG_PATH ConfigStore::kDefaultPath
#endif

#ifndef DEFAULT_PROMPTS_DIR
#define DEFAULT_PROMPTS_DIR nullptr
#endif

namespace {

std::atomic<bool> g_app_running{true};

void on_sig_int(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n⏹ Nhận tín hiệu dừng, đang thoát..." << std::endl;
        g_app_running = false;
    }
}

} // namespace

int main() {
    std::signal(SIGINT, on_sig_int);

    WhisperEngine whisper(DEFAULT_MODEL_PATH);
    if (!whisper.isLoaded()) {
        return 1;
    }

    AppConfig app_config = AppConfig::load(DEFAULT_CONFIG_PATH, DEFAULT_PROMPTS_DIR);

    AudioCapture capture;
    if (!capture.open(g_app_running, app_config.audio)) {
        return 1;
    }
    CommandProcessor commands(std::move(app_config.ai));
    VoiceListener listener(whisper, capture, commands, g_app_running, DEFAULT_PIPER_MODEL_PATH);
    listener.run();

    capture.close();
    SDL_Quit();
    std::cout << "\n✅ Đã thoát." << std::endl;
    return 0;
}
