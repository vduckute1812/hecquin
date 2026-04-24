#pragma once

#include "actions/Action.hpp"
#include "config/ai/AiClientConfig.hpp"

#include <future>
#include <string>

namespace hecquin::ai {
class IHttpClient;
}

namespace hecquin::learning {

class LearningStore;
class EmbeddingClient;
class RetrievalService;
class ProgressTracker;

struct EnglishTutorConfig {
    int rag_top_k = 3;
    int rag_max_context_chars = 3500;
    int http_timeout_seconds = 60;
};

/**
 * The lesson-mode counterpart of `CommandProcessor`:
 *  transcript → RAG context → Gemini chat → GrammarCorrectionAction.
 *
 * Also writes each round-trip to `ProgressTracker`.
 */
class EnglishTutorProcessor {
public:
    EnglishTutorProcessor(AiClientConfig ai,
                          RetrievalService& retrieval,
                          ProgressTracker& progress,
                          EnglishTutorConfig cfg = {});

    /**
     * Inject a custom HTTP client — used by tests to return canned
     * responses without touching the network.  Nullptr (the default) falls
     * back to the libcurl-backed `http_post_json` free function.
     */
    void set_http_client_for_test(hecquin::ai::IHttpClient* client) {
        http_client_ = client;
    }

    Action process(const std::string& transcript);
    std::future<Action> process_async(const std::string& transcript);

    bool ready() const;
    const AiClientConfig& config() const { return ai_; }

private:
    std::string build_chat_body_(const std::string& user_text, const std::string& context) const;
    Action call_llm_(const std::string& user_text);

    AiClientConfig ai_;
    RetrievalService& retrieval_;
    ProgressTracker& progress_;
    EnglishTutorConfig cfg_;
    hecquin::ai::IHttpClient* http_client_ = nullptr;
};

} // namespace hecquin::learning
