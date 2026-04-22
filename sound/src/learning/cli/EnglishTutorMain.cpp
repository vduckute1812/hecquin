#include "ai/CommandProcessor.hpp"
#include "ai/IHttpClient.hpp"
#include "ai/LoggingHttpClient.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/EnglishTutorProcessor.hpp"
#include "learning/store/LearningStore.hpp"
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
    const bool store_open = store.open();
    if (!store_open) {
        std::cerr << "[english_tutor] Learning DB unavailable; continuing without RAG / progress."
                  << std::endl;
    }

    // Decorator chain: CurlHttpClient  →  LoggingHttpClient  →  {ChatClient, EmbeddingClient}
    // When the DB opened we bind the sink to `record_api_call`; otherwise the
    // decorator becomes a transparent passthrough.
    hecquin::ai::CurlHttpClient      raw_http;
    hecquin::ai::ApiCallSink         sink;
    if (store_open) {
        sink = [&store](const hecquin::ai::ApiCallRecord& r) {
            store.record_api_call(r.provider, r.endpoint, r.method,
                                  r.status, r.latency_ms,
                                  r.request_bytes, r.response_bytes,
                                  r.ok, r.error);
        };
    }
    hecquin::ai::LoggingHttpClient   http(raw_http, "chat", sink);
    hecquin::ai::LoggingHttpClient   embed_http(raw_http, "embedding", sink);

    hecquin::learning::EmbeddingClient   embedder(cfg.ai, embed_http);
    hecquin::learning::RetrievalService  retrieval(store, embedder);
    hecquin::learning::ProgressTracker   progress(store, "lesson");

    hecquin::learning::EnglishTutorConfig tcfg;
    tcfg.rag_top_k = cfg.learning.rag_top_k;
    hecquin::learning::EnglishTutorProcessor tutor(cfg.ai, retrieval, progress, tcfg);

    CommandProcessor commands(cfg.ai, http);
    VoiceListener listener(app.whisper(), app.capture(), commands,
                           app.running(), app.piper_model_path());
    listener.setTutorCallback([&tutor](const Utterance& u) {
        return tutor.process(u.transcript);
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
