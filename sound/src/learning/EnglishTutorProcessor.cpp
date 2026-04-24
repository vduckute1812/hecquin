#include "learning/EnglishTutorProcessor.hpp"

#include "actions/GrammarCorrectionAction.hpp"
#include "ai/HttpClient.hpp"
#include "ai/HttpReplyBuckets.hpp"
#include "ai/IHttpClient.hpp"
#include "ai/OpenAiChatContent.hpp"
#include "common/StringUtils.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/store/LearningStore.hpp"
#include "learning/ProgressTracker.hpp"
#include "learning/RetrievalService.hpp"

#include <nlohmann/json.hpp>

#include <regex>

namespace hecquin::learning {

using hecquin::common::trim_copy;

namespace {

// Parse "You said: ... / Better: ... / Reason: ..." (or any casing / punctuation variant).
GrammarCorrectionAction parse_tutor_reply(const std::string& raw, const std::string& fallback_original) {
    GrammarCorrectionAction out;
    out.original = fallback_original;

    auto find_line = [&](const std::regex& re) -> std::string {
        std::smatch m;
        if (std::regex_search(raw, m, re)) {
            return trim_copy(m[1].str());
        }
        return {};
    };

    const std::string you =
        find_line(std::regex(R"(you\s*said\s*[:\-]\s*(.+))", std::regex_constants::icase));
    const std::string better =
        find_line(std::regex(R"(better\s*[:\-]\s*(.+))", std::regex_constants::icase));
    const std::string reason =
        find_line(std::regex(R"(reason\s*[:\-]\s*(.+))", std::regex_constants::icase));

    if (!you.empty()) out.original = you;
    out.corrected = better.empty() ? out.original : better;
    out.explanation = reason;

    // If absolutely nothing matched, put the whole model text into explanation.
    if (better.empty() && reason.empty()) {
        out.explanation = trim_copy(raw);
    }
    return out;
}

} // namespace

EnglishTutorProcessor::EnglishTutorProcessor(AiClientConfig ai,
                                             RetrievalService& retrieval,
                                             ProgressTracker& progress,
                                             EnglishTutorConfig cfg)
    : ai_(std::move(ai)), retrieval_(retrieval), progress_(progress), cfg_(cfg) {}

bool EnglishTutorProcessor::ready() const { return ai_.ready(); }

std::string EnglishTutorProcessor::build_chat_body_(const std::string& user_text,
                                                    const std::string& context) const {
    std::string system = ai_.tutor_system_prompt;
    if (!context.empty()) {
        system += "\n\nReference snippets (use only if relevant):\n" + context;
    }
    const nlohmann::json body = {
        {"model", ai_.model},
        {"messages", nlohmann::json::array({
            {{"role", "system"}, {"content", system}},
            {{"role", "user"},   {"content", user_text}},
        })},
    };
    // RAG context can contain stray non-UTF-8 bytes from ingested corpora;
    // replace them with U+FFFD rather than aborting the tutor turn.
    return body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

Action EnglishTutorProcessor::call_llm_(const std::string& user_text) {
    const std::string trimmed = trim_copy(user_text);
    if (trimmed.empty()) {
        Action a;
        a.kind = ActionKind::None;
        a.transcript = user_text;
        return a;
    }

    const std::string context =
        retrieval_.build_context(trimmed, cfg_.rag_top_k, cfg_.rag_max_context_chars);

    if (!ai_.ready()) {
        GrammarCorrectionAction fallback;
        fallback.original = trimmed;
        fallback.corrected = trimmed;
        fallback.explanation = "Cloud API is not configured, so I cannot check grammar yet.";
        progress_.log_interaction(trimmed, fallback.corrected, fallback.explanation);
        return fallback.into_action(trimmed);
    }

    const std::string body = build_chat_body_(trimmed, context);
    const auto result = http_client_
        ? http_client_->post_json(ai_.chat_completions_url, ai_.api_key, body,
                                  cfg_.http_timeout_seconds)
        : http_post_json(ai_.chat_completions_url, ai_.api_key, body,
                         cfg_.http_timeout_seconds);
    if (!result) {
        GrammarCorrectionAction err;
        err.original = trimmed;
        err.corrected = trimmed;
        err.explanation = "I could not reach the tutor service.";
        progress_.log_interaction(trimmed, err.corrected, err.explanation);
        return err.into_action(trimmed);
    }
    if (result->status < 200 || result->status >= 300) {
        GrammarCorrectionAction err;
        err.original = trimmed;
        err.corrected = trimmed;
        err.explanation = hecquin::ai::short_reply_for_status(result->status);
        progress_.log_interaction(trimmed, err.corrected, err.explanation);
        return err.into_action(trimmed);
    }

    const auto content = extract_openai_chat_assistant_content(result->body);
    if (!content) {
        GrammarCorrectionAction err;
        err.original = trimmed;
        err.corrected = trimmed;
        err.explanation = "The tutor reply could not be parsed.";
        progress_.log_interaction(trimmed, err.corrected, err.explanation);
        return err.into_action(trimmed);
    }

    const auto parsed = parse_tutor_reply(*content, trimmed);
    progress_.log_interaction(trimmed, parsed.corrected, parsed.explanation);
    return parsed.into_action(trimmed);
}

Action EnglishTutorProcessor::process(const std::string& transcript) {
    return call_llm_(transcript);
}

std::future<Action> EnglishTutorProcessor::process_async(const std::string& transcript) {
    return std::async(std::launch::async, [this, transcript]() { return process(transcript); });
}

} // namespace hecquin::learning
