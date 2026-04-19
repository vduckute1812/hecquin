#include "ai/ChatClient.hpp"

#include "actions/ExternalApiAction.hpp"
#include "ai/IHttpClient.hpp"
#include "ai/OpenAiChatContent.hpp"

#include <nlohmann/json.hpp>

namespace hecquin::ai {

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
    return body.dump();
}

Action ChatClient::ask(const std::string& user_text) const {
    if (!config_.ready()) {
        return ExternalApiAction::with_reply(
            "Set OPENAI_API_KEY, HECQUIN_AI_API_KEY, GEMINI_API_KEY, or "
            "GOOGLE_API_KEY for cloud replies.",
            user_text);
    }

    const std::string body = build_request_body(user_text);
    const auto result = http_.post_json(config_.chat_completions_url, config_.api_key, body);

    if (!result) {
        return ExternalApiAction::with_reply("HTTP request failed.", user_text);
    }
    if (result->status < 200 || result->status >= 300) {
        return ExternalApiAction::with_reply(
            "API error HTTP " + std::to_string(result->status) + ": " +
            result->body.substr(0, 500),
            user_text);
    }

    const auto content = extract_openai_chat_assistant_content(result->body);
    if (!content) {
        return ExternalApiAction::with_reply("Could not parse model reply.", user_text);
    }
    return ExternalApiAction::with_reply(*content, user_text);
}

} // namespace hecquin::ai
