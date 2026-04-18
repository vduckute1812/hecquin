#include "ai/CommandProcessor.hpp"
#include "actions/NoneAction.hpp"
#include "actions/DeviceAction.hpp"
#include "actions/ExternalApiAction.hpp"
#include "actions/MusicAction.hpp"
#include "actions/TopicSearchAction.hpp"
#include "ai/HttpClient.hpp"
#include "ai/OpenAiChatContent.hpp"

#include <algorithm>
#include <cctype>
#include <future>
#include <iomanip>
#include <regex>
#include <sstream>
#include <iostream>

namespace {

std::string trim_copy(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string to_lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    std::ostringstream oss;
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    out += oss.str();
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

} // namespace

CommandProcessor::CommandProcessor(AiClientConfig config) : config_(std::move(config)) {}

std::optional<Action> CommandProcessor::match_local_(const std::string& n) const {
    static const std::regex re_device(
        R"(^\s*(turn on|turn off)\s+(air|switch)\b)",
        std::regex_constants::ECMAScript | std::regex_constants::icase);

    static const std::regex re_story(
        R"(\btell me a story\b)",
        std::regex_constants::ECMAScript | std::regex_constants::icase);

    static const std::regex re_music(
        R"(\bopen music\b)",
        std::regex_constants::ECMAScript | std::regex_constants::icase);

    std::smatch m;
    if (std::regex_search(n, m, re_device)) {
        const std::string verb = to_lower_copy(trim_copy(std::string(m[1].first, m[1].second)));
        const std::string target = to_lower_copy(trim_copy(std::string(m[2].first, m[2].second)));
        const DevicePowerVerb power = (verb == "turn on") ? DevicePowerVerb::TurnOn : DevicePowerVerb::TurnOff;
        const DeviceOption device = (target == "air") ? DeviceOption::AirConditioning : DeviceOption::Switch;
        return DeviceAction{power, device}.into_action(n);
    }

    if (std::regex_search(n, re_story)) {
        return TopicSearchAction{}.into_action(n);
    }

    if (std::regex_search(n, re_music)) {
        return MusicAction{}.into_action(n);
    }

    return std::nullopt;
}

std::string CommandProcessor::build_chat_body_(const std::string& user_text) const {
    return std::string("{\"model\":\"") + json_escape(config_.model) +
           "\",\"messages\":["
           "{\"role\":\"system\",\"content\":\"" + json_escape(config_.system_prompt) + "\"},"
           "{\"role\":\"user\",\"content\":\"" + json_escape(user_text) +
           "\"}]}";
}

Action CommandProcessor::call_external_api_(const std::string& user_text) const {
    if (!config_.ready()) {
        return ExternalApiAction::with_reply(
            "Set OPENAI_API_KEY, HECQUIN_AI_API_KEY, GEMINI_API_KEY, or GOOGLE_API_KEY for cloud replies.",
            user_text);
    }

    const std::string body = build_chat_body_(user_text);
    const auto result = http_post_json(config_.chat_completions_url, config_.api_key, body);

    if (!result) {
        return ExternalApiAction::with_reply("HTTP request failed.", user_text);
    }

    if (result->status < 200 || result->status >= 300) {
        return ExternalApiAction::with_reply(
            "API error HTTP " + std::to_string(result->status) + ": " + result->body.substr(0, 500),
            user_text);
    }

    const auto content = extract_openai_chat_assistant_content(result->body);
    if (!content) {
        return ExternalApiAction::with_reply("Could not parse model reply.", user_text);
    }
    return ExternalApiAction::with_reply(*content, user_text);
}

Action CommandProcessor::process(const std::string& transcript) {
    const std::string trimmed = trim_copy(transcript);
    const std::string normalized = to_lower_copy(trimmed);
    if (normalized.empty()) {
        return NoneAction::empty_transcript();
    }

    if (auto local = match_local_(normalized)) {
        local->transcript = trimmed;
        return *local;
    }

    return call_external_api_(trimmed);
}

std::future<Action> CommandProcessor::process_async(const std::string& transcript) {
    return std::async(std::launch::async, [this, transcript]() { return process(transcript); });
}
