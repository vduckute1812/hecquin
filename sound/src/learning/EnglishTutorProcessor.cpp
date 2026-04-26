#include "learning/EnglishTutorProcessor.hpp"

#include "actions/GrammarCorrectionAction.hpp"
#include "ai/HttpClient.hpp"
#include "ai/HttpReplyBuckets.hpp"
#include "ai/IHttpClient.hpp"
#include "ai/OpenAiChatContent.hpp"
#include "common/StringUtils.hpp"
#include "learning/ProgressTracker.hpp"
#include "learning/RetrievalService.hpp"
#include "learning/tutor/TutorChatRequest.hpp"
#include "learning/tutor/TutorContextBuilder.hpp"
#include "learning/tutor/TutorReplyParser.hpp"

namespace hecquin::learning {

using hecquin::common::trim_copy;

EnglishTutorProcessor::EnglishTutorProcessor(AiClientConfig ai,
                                             RetrievalService& retrieval,
                                             ProgressTracker& progress,
                                             EnglishTutorConfig cfg)
    : ai_(std::move(ai)), retrieval_(retrieval), progress_(progress), cfg_(cfg) {}

bool EnglishTutorProcessor::ready() const { return ai_.ready(); }

namespace {

Action make_fallback(const std::string& trimmed,
                     const std::string& explanation,
                     ProgressTracker& progress) {
    GrammarCorrectionAction err;
    err.original = trimmed;
    err.corrected = trimmed;
    err.explanation = explanation;
    progress.log_interaction(trimmed, err.corrected, err.explanation);
    return err.into_action(trimmed);
}

} // namespace

std::string EnglishTutorProcessor::build_chat_body_(const std::string& user_text,
                                                    const std::string& context) const {
    return tutor::build_chat_body(ai_, user_text, context);
}

Action EnglishTutorProcessor::call_llm_(const std::string& user_text) {
    const std::string trimmed = trim_copy(user_text);
    if (trimmed.empty()) {
        Action a;
        a.kind = ActionKind::None;
        a.transcript = user_text;
        return a;
    }

    const tutor::TutorContextBuilder ctx_builder(retrieval_,
                                                 cfg_.rag_top_k,
                                                 cfg_.rag_max_context_chars);
    const std::string context = ctx_builder.build(trimmed);

    if (!ai_.ready()) {
        return make_fallback(
            trimmed,
            "Cloud API is not configured, so I cannot check grammar yet.",
            progress_);
    }

    const std::string body = build_chat_body_(trimmed, context);
    const auto result = http_client_
        ? http_client_->post_json(ai_.chat_completions_url, ai_.api_key, body,
                                  cfg_.http_timeout_seconds)
        : http_post_json(ai_.chat_completions_url, ai_.api_key, body,
                         cfg_.http_timeout_seconds);
    if (!result) {
        return make_fallback(trimmed, "I could not reach the tutor service.",
                             progress_);
    }
    if (result->status < 200 || result->status >= 300) {
        return make_fallback(trimmed,
                             hecquin::ai::short_reply_for_status(result->status),
                             progress_);
    }

    const auto content = extract_openai_chat_assistant_content(result->body);
    if (!content) {
        return make_fallback(trimmed, "The tutor reply could not be parsed.",
                             progress_);
    }

    const auto parsed = tutor::parse_tutor_reply(*content, trimmed);
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
