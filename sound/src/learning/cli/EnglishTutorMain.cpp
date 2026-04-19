#include "ai/CommandProcessor.hpp"
#include "config/AppConfig.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/EnglishTutorProcessor.hpp"
#include "learning/LearningStore.hpp"
#include "learning/ProgressTracker.hpp"
#include "learning/RetrievalService.hpp"
#include "voice/AudioCapture.hpp"
#include "voice/VoiceListener.hpp"
#include "voice/WhisperEngine.hpp"

#include <SDL.h>

#include <atomic>
#include <csignal>
#include <iostream>

#ifndef DEFAULT_MODEL_PATH
#define DEFAULT_MODEL_PATH ".env/shared/models/ggml-base.bin"
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

    AppConfig app = AppConfig::load(DEFAULT_CONFIG_PATH, DEFAULT_PROMPTS_DIR);

    AudioCapture capture;
    if (!capture.open(g_app_running, app.audio)) {
        return 1;
    }

    hecquin::learning::LearningStore store(app.learning.db_path, app.ai.embedding_dim);
    if (!store.open()) {
        std::cerr << "⚠️  Learning DB unavailable; continuing without RAG / progress." << std::endl;
    }

    hecquin::learning::EmbeddingClient embedder(app.ai);
    hecquin::learning::RetrievalService retrieval(store, embedder);
    hecquin::learning::ProgressTracker progress(store, "lesson");

    hecquin::learning::EnglishTutorConfig tcfg;
    tcfg.rag_top_k = app.learning.rag_top_k;
    hecquin::learning::EnglishTutorProcessor tutor(app.ai, retrieval, progress, tcfg);

    CommandProcessor commands(app.ai);
    VoiceListener listener(whisper, capture, commands, g_app_running, DEFAULT_PIPER_MODEL_PATH);
    listener.set_tutor_callback([&tutor](const std::string& transcript) {
        return tutor.process(transcript);
    });
    listener.set_initial_mode(ListenerMode::Lesson);

    std::cout << "🎓 English tutor mode enabled. Say 'exit lesson' to stop learning.\n";
    listener.run();

    progress.close();
    capture.close();
    SDL_Quit();
    std::cout << "\n✅ Đã thoát." << std::endl;
    return 0;
}
