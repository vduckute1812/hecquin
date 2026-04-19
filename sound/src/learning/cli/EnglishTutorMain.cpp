#include "ai/CommandProcessor.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/EnglishTutorProcessor.hpp"
#include "learning/LearningStore.hpp"
#include "learning/ProgressTracker.hpp"
#include "learning/RetrievalService.hpp"
#include "voice/VoiceApp.hpp"
#include "voice/VoiceListener.hpp"

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
#define DEFAULT_PROMPTS_DIR ""
#endif

int main() {
    hecquin::voice::VoiceApp app({
        DEFAULT_MODEL_PATH,
        DEFAULT_PIPER_MODEL_PATH,
        DEFAULT_CONFIG_PATH,
        DEFAULT_PROMPTS_DIR,
    });
    if (!app.init()) return 1;

    AppConfig& cfg = app.config();

    hecquin::learning::LearningStore store(cfg.learning.db_path, cfg.ai.embedding_dim);
    if (!store.open()) {
        std::cerr << "[english_tutor] Learning DB unavailable; continuing without RAG / progress."
                  << std::endl;
    }

    hecquin::learning::EmbeddingClient   embedder(cfg.ai);
    hecquin::learning::RetrievalService  retrieval(store, embedder);
    hecquin::learning::ProgressTracker   progress(store, "lesson");

    hecquin::learning::EnglishTutorConfig tcfg;
    tcfg.rag_top_k = cfg.learning.rag_top_k;
    hecquin::learning::EnglishTutorProcessor tutor(cfg.ai, retrieval, progress, tcfg);

    CommandProcessor commands(cfg.ai);
    VoiceListener listener(app.whisper(), app.capture(), commands,
                           app.running(), app.piper_model_path());
    listener.setTutorCallback([&tutor](const std::string& transcript) {
        return tutor.process(transcript);
    });
    listener.setInitialMode(ListenerMode::Lesson);

    std::cout << "[english_tutor] Lesson mode enabled — say 'exit lesson' to stop learning."
              << std::endl;
    listener.run();

    progress.close();
    app.shutdown();
    std::cout << "\n[english_tutor] exited cleanly." << std::endl;
    return 0;
}
