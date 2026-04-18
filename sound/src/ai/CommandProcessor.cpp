#include "ai/CommandProcessor.hpp"
#include "actions/NoneAction.hpp"
#include "actions/DeviceAction.hpp"
#include "actions/ExternalApiAction.hpp"
#include "actions/MusicAction.hpp"
#include "actions/TopicSearchAction.hpp"
#include "ai/OpenAiChatContent.hpp"

#include <algorithm>
#include <cctype>
#include <future>
#include <iomanip>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <iostream>

#ifdef HECQUIN_WITH_CURL
#include <curl/curl.h>
#endif

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

#ifdef HECQUIN_WITH_CURL
size_t curl_write_string(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

struct CurlEasyDeleter {
    void operator()(CURL* c) const noexcept { curl_easy_cleanup(c); }
};

struct CurlSlistDeleter {
    void operator()(curl_slist* s) const noexcept { curl_slist_free_all(s); }
};

using CurlEasyPtr = std::unique_ptr<CURL, CurlEasyDeleter>;
using CurlSlistPtr = std::unique_ptr<curl_slist, CurlSlistDeleter>;
#endif

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

Action CommandProcessor::call_external_api_(const std::string& user_text) const {
#ifndef HECQUIN_WITH_CURL
    return ExternalApiAction::with_reply(
        "External API is unavailable in this build (libcurl not found at configure time).", user_text);
#else
    if (!config_.ready()) {
        return ExternalApiAction::with_reply(
            "Set OPENAI_API_KEY, HECQUIN_AI_API_KEY, GEMINI_API_KEY, or GOOGLE_API_KEY for cloud replies.",
            user_text);
    }

    static std::once_flag curl_init_once;
    std::call_once(curl_init_once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

    static constexpr const char* kSystemPrompt =
        "You are a voice assistant on a smart speaker. "
        "Reply in 1-3 short sentences using plain spoken language. "
        "No markdown, no bullet points, no lists, no special formatting.";

    const std::string body = std::string("{\"model\":\"") + json_escape(config_.model) +
                             "\",\"messages\":["
                             "{\"role\":\"system\",\"content\":\"" + json_escape(kSystemPrompt) + "\"},"
                             "{\"role\":\"user\",\"content\":\"" + json_escape(user_text) +
                             "\"}]}";

    std::string response;
    CurlEasyPtr curl(curl_easy_init());
    if (!curl) {
        return ExternalApiAction::with_reply("Failed to initialize HTTP client.", user_text);
    }

    curl_slist* header_list = nullptr;
    header_list = curl_slist_append(header_list, "Content-Type: application/json");
    const std::string auth = "Authorization: Bearer " + config_.api_key;
    header_list = curl_slist_append(header_list, auth.c_str());
    CurlSlistPtr headers(header_list);

    std::cout << "api_key: " << config_.api_key << std::endl;
    std::cout << "chat_completions_url: " << config_.chat_completions_url << std::endl;
    std::cout << "body: " << body << std::endl;

    curl_easy_setopt(curl.get(), CURLOPT_URL, config_.chat_completions_url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, curl_write_string);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 60L);

    const CURLcode res = curl_easy_perform(curl.get());
    long http_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        return ExternalApiAction::with_reply(std::string("Request failed: ") + curl_easy_strerror(res), user_text);
    }

    if (http_code < 200 || http_code >= 300) {
        return ExternalApiAction::with_reply("API error HTTP " + std::to_string(http_code) + ": " + response.substr(0, 500),
                                             user_text);
    }

    const auto content = extract_openai_chat_assistant_content(response);
    if (!content) {
        return ExternalApiAction::with_reply("Could not parse model reply.", user_text);
    }
    return ExternalApiAction::with_reply(*content, user_text);
#endif
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
