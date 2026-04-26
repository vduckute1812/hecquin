#include "ai/ChatClient.hpp"

#include "actions/ExternalApiAction.hpp"
#include "ai/HttpReplyBuckets.hpp"
#include "ai/IHttpClient.hpp"
#include "ai/OpenAiChatContent.hpp"
#include "common/EnvParse.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <string>

namespace hecquin::ai {

ChatClient::ChatClient(AiClientConfig config, IHttpClient& http)
    : config_(std::move(config)), http_(http) {
    apply_env_overrides();
}

void ChatClient::apply_env_overrides() {
    namespace env = hecquin::common::env;
    int iv = 0;
    if (env::parse_int("HECQUIN_CHAT_COOLDOWN_FAILURES", iv)) {
        failure_threshold_ = std::max(1, iv);
    }
    if (env::parse_int("HECQUIN_CHAT_COOLDOWN_WINDOW_MS", iv)) {
        failure_window_ = std::chrono::milliseconds(std::max(0, iv));
    }
    if (env::parse_int("HECQUIN_CHAT_COOLDOWN_DURATION_MS", iv)) {
        cooldown_duration_ = std::chrono::milliseconds(std::max(0, iv));
    }
}

bool ChatClient::in_cooldown(Clock::time_point now) const {
    return now < cooldown_until_;
}

void ChatClient::note_failure_(Clock::time_point now) {
    if (consecutive_failures_ == 0 ||
        now - first_failure_at_ > failure_window_) {
        consecutive_failures_ = 1;
        first_failure_at_ = now;
    } else {
        ++consecutive_failures_;
    }
    if (consecutive_failures_ >= failure_threshold_) {
        cooldown_until_ = now + cooldown_duration_;
        std::cerr << "[ChatClient] entering cooldown for "
                  << cooldown_duration_.count() << " ms after "
                  << consecutive_failures_ << " consecutive failures."
                  << std::endl;
    }
}

void ChatClient::note_success_() {
    consecutive_failures_ = 0;
    cooldown_until_ = Clock::time_point{};
}

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

Action ChatClient::ask(const std::string& user_text) {
    if (!config_.ready()) {
        std::cerr << "[ChatClient] API key missing — set OPENAI_API_KEY, "
                     "HECQUIN_AI_API_KEY, GEMINI_API_KEY or GOOGLE_API_KEY."
                  << std::endl;
        return ExternalApiAction::with_reply(
            "Sorry, the AI service is not configured.", user_text);
    }

    const auto now = Clock::now();
    if (in_cooldown(now)) {
        // Skip the HTTP round-trip entirely while we know the network
        // is down — saves the user from an 8-second silence on every
        // utterance during a Wi-Fi blip.
        return ExternalApiAction::with_reply(
            "I can't reach the cloud right now. Try a local command, or say "
            "help.",
            user_text);
    }

    const std::string body = build_request_body(user_text);
    const auto result = http_.post_json(config_.chat_completions_url, config_.api_key, body);

    if (!result) {
        std::cerr << "[ChatClient] HTTP request failed (transport error)."
                  << std::endl;
        note_failure_(now);
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
        // Only count 5xx / network-bucket statuses toward cooldown;
        // 4xx is the user's fault (bad key, bad model) and won't fix
        // itself just by waiting.
        if (result->status >= 500) note_failure_(now);
        return ExternalApiAction::with_reply(
            short_reply_for_status(result->status), user_text);
    }

    note_success_();

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
