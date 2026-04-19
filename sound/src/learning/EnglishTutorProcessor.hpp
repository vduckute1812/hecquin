#pragma once

#include "actions/Action.hpp"
#include "config/ai/AiClientConfig.hpp"

#include <future>
#include <string>

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
};

} // namespace hecquin::learning
