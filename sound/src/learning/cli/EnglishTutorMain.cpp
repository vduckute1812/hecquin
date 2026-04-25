#include "ai/CommandProcessor.hpp"
#include "ai/IHttpClient.hpp"
#include "ai/LoggingHttpClient.hpp"
#include "ai/RetryingHttpClient.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/EnglishTutorProcessor.hpp"
#include "learning/ProgressTracker.hpp"
#include "learning/PronunciationDrillProcessor.hpp"
#include "learning/RetrievalService.hpp"
#include "learning/cli/LearningApp.hpp"
#include "learning/store/LearningStore.hpp"
#include "music/MusicFactory.hpp"
#include "music/MusicSession.hpp"
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
#ifndef DEFAULT_PRONUNCIATION_MODEL_PATH
#define DEFAULT_PRONUNCIATION_MODEL_PATH ""
#endif
#ifndef DEFAULT_PRONUNCIATION_VOCAB_PATH
#define DEFAULT_PRONUNCIATION_VOCAB_PATH ""
#endif

int main() {
    hecquin::learning::cli::LearningApp app({
        /*whisper_model_path=*/DEFAULT_MODEL_PATH,
        /*piper_model_path=*/DEFAULT_PIPER_MODEL_PATH,
        /*config_path=*/DEFAULT_CONFIG_PATH,
        /*prompts_dir=*/DEFAULT_PROMPTS_DIR,
        /*baked_pron_model=*/DEFAULT_PRONUNCIATION_MODEL_PATH,
        /*baked_pron_vocab=*/DEFAULT_PRONUNCIATION_VOCAB_PATH,
        /*progress_mode=*/"lesson",
        /*open_secondary_drill_progress=*/true,
    });
    if (!app.init()) return 1;

    AppConfig& cfg = app.config();
    auto& store = app.store();

    // Decorator chain: CurlHttpClient → LoggingHttpClient → RetryingHttpClient.
    // The retrier sits *outside* the logger so every HTTP attempt — including
    // the failed ones the retrier re-issues — shows up as its own `api_calls`
    // row.  That makes rate-limit churn visible on the dashboard.
    hecquin::ai::CurlHttpClient      raw_http;
    hecquin::ai::ApiCallSink         sink;
    if (app.store_open()) {
        sink = [&store](const hecquin::ai::ApiCallRecord& r) {
            store.record_api_call(r.provider, r.endpoint, r.method,
                                  r.status, r.latency_ms,
                                  r.request_bytes, r.response_bytes,
                                  r.ok, r.error);
        };
    }
    hecquin::ai::LoggingHttpClient   logged_chat_http(raw_http, "chat", sink);
    hecquin::ai::LoggingHttpClient   logged_embed_http(raw_http, "embedding", sink);
    hecquin::ai::RetryPolicy         retry_policy;
    retry_policy.apply_env_overrides();
    hecquin::ai::RetryingHttpClient  http(logged_chat_http, retry_policy);
    hecquin::ai::RetryingHttpClient  embed_http(logged_embed_http, retry_policy);

    hecquin::learning::EmbeddingClient   embedder(cfg.ai, embed_http);
    hecquin::learning::RetrievalService  retrieval(store, embedder);

    hecquin::learning::EnglishTutorConfig tcfg;
    tcfg.rag_top_k = cfg.learning.rag_top_k;
    hecquin::learning::EnglishTutorProcessor tutor(cfg.ai, retrieval, app.progress(), tcfg);

    auto matcher_cfg = app.matcher_config();
    CommandProcessor commands(cfg.ai, http, std::move(matcher_cfg));
    VoiceListenerConfig vcfg;
    vcfg.apply_env_overrides();
    VoiceListener listener(app.voice().whisper(), app.voice().capture(), commands,
                           app.voice().running(), app.piper_model_path(), vcfg);
    app.wire_pipeline_sink(listener);
    app.wire_drill_callbacks(listener);
    listener.setTutorCallback([&tutor](const Utterance& u) {
        return tutor.process(u.transcript);
    });

    auto music_provider = hecquin::music::make_provider_from_config(cfg.music);
    hecquin::music::MusicSession music_session(*music_provider, &app.voice().capture());
    listener.setMusicCallback([&music_session](const std::string& q) {
        return music_session.handle(q);
    });

    listener.setInitialMode(ListenerMode::Lesson);
    // "exit drill" returns to Lesson (the binary's home) instead of falling
    // through to the generic Assistant / chat-completions fallback.
    listener.setHomeMode(ListenerMode::Lesson);

    std::cout << "[english_tutor] Lesson mode enabled — say 'exit lesson' to stop learning,\n"
              << "                or 'begin pronunciation' to enter the drill."
              << std::endl;
    listener.run();

    app.shutdown();
    std::cout << "\n[english_tutor] exited cleanly." << std::endl;
    return 0;
}
