#include "ai/ChatClient.hpp"

#include "actions/ExternalApiAction.hpp"
#include "ai/IHttpClient.hpp"
#include "ai/OpenAiChatContent.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

namespace hecquin::ai {

namespace {

// Map an HTTP status code to a short phrase safe to read aloud. The full
// response body is logged separately to stderr for operators — we don't want
// to dump it through Piper.
std::string short_reply_for_status(int status) {
    if (status == 401 || status == 403) {
        return "Sorry, the AI service rejected the API key.";
    }
    if (status == 404) {
        return "Sorry, the AI service endpoint was not found.";
    }
    if (status == 408 || status == 504) {
        return "Sorry, the AI service timed out.";
    }
    if (status == 429) {
        return "Sorry, the AI service is busy. Please try again in a moment.";
    }
    if (status >= 500 && status < 600) {
        return "Sorry, the AI service is temporarily unavailable.";
    }
    if (status >= 400 && status < 500) {
        return "Sorry, the AI service rejected the request.";
    }
    return "Sorry, the AI service returned an error.";
}

} // namespace

ChatClient::ChatClient(AiClientConfig config, IHttpClient& http)
    : config_(std::move(config)), http_(http) {}

std::string ChatClient::build_request_body(const std::string& user_text) const {
    const nlohmann::json body = {
        {"model", config_.model},
        {"messages", nlohmann::json::array({
            {{"role", "system"}, {"content", config_.system_prompt}},
            {{"role", "user"},   {"content", user_text}},
        })},
    };
    // Tolerate accidental non-UTF-8 in Whisper transcripts or system prompts
    // by replacing bad bytes with U+FFFD instead of throwing.
    return body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

Action ChatClient::ask(const std::string& user_text) const {
    if (!config_.ready()) {
        std::cerr << "[ChatClient] API key missing — set OPENAI_API_KEY, "
                     "HECQUIN_AI_API_KEY, GEMINI_API_KEY or GOOGLE_API_KEY."
                  << std::endl;
        return ExternalApiAction::with_reply(
            "Sorry, the AI service is not configured.", user_text);
    }

    const std::string body = build_request_body(user_text);
    const auto result = http_.post_json(config_.chat_completions_url, config_.api_key, body);

    if (!result) {
        std::cerr << "[ChatClient] HTTP request failed (transport error)."
                  << std::endl;
        return ExternalApiAction::with_reply(
            "Sorry, I could not reach the AI service. Please check the "
            "network connection.",
            user_text);
    }
    if (result->status < 200 || result->status >= 300) {
        // Dump the full server response to stderr for debugging, but keep the
        // spoken reply short so Piper doesn't read the raw JSON aloud.
        std::cerr << "[ChatClient] HTTP " << result->status
                  << " from " << config_.chat_completions_url << '\n'
                  << "[ChatClient] body: "
                  << result->body.substr(0, 2000) << std::endl;
        return ExternalApiAction::with_reply(
            short_reply_for_status(result->status), user_text);
    }

    const auto content = extract_openai_chat_assistant_content(result->body);
    if (!content) {
        std::cerr << "[ChatClient] Could not parse model reply; body: "
                  << result->body.substr(0, 2000) << std::endl;
        return ExternalApiAction::with_reply(
            "Sorry, I could not understand the AI service reply.",
            user_text);
    }
    return ExternalApiAction::with_reply(*content, user_text);
}

} // namespace hecquin::ai
