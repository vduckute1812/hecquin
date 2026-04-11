#include "ai/CommandProcessor.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>

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

std::string getenv_string(const char* key) {
    const char* v = std::getenv(key);
    return v ? std::string(v) : std::string();
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

/** Very small parser: find "\"content\":\"" in first choice message and read until unescaped quote. */
std::string extract_chat_content(const std::string& json) {
    const std::string key = "\"content\":";
    size_t pos = json.find(key);
    if (pos == std::string::npos) {
        return {};
    }
    pos = json.find('"', pos + key.size());
    if (pos == std::string::npos) {
        return {};
    }
    ++pos;
    std::string out;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '\\' && pos + 1 < json.size()) {
            char n = json[pos + 1];
            if (n == 'n') {
                out += '\n';
                pos += 2;
                continue;
            }
            if (n == 't') {
                out += '\t';
                pos += 2;
                continue;
            }
            if (n == 'r') {
                out += '\r';
                pos += 2;
                continue;
            }
            if (n == '"' || n == '\\') {
                out += n;
                pos += 2;
                continue;
            }
            out += c;
            ++pos;
            continue;
        }
        if (c == '"') {
            break;
        }
        out += c;
        ++pos;
    }
    return out;
}

#ifdef HECQUIN_WITH_CURL
size_t curl_write_string(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}
#endif

} // namespace

AiClientConfig AiClientConfig::from_environment() {
    AiClientConfig c;
    c.api_key = getenv_string("OPENAI_API_KEY");
    if (c.api_key.empty()) {
        c.api_key = getenv_string("HECQUIN_AI_API_KEY");
    }
    std::string base = getenv_string("OPENAI_BASE_URL");
    if (base.empty()) {
        base = getenv_string("HECQUIN_AI_BASE_URL");
    }
    if (base.empty()) {
        base = "https://api.openai.com/v1";
    }
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    c.chat_completions_url = base + "/chat/completions";

    std::string model = getenv_string("HECQUIN_AI_MODEL");
    if (model.empty()) {
        model = getenv_string("OPENAI_MODEL");
    }
    if (!model.empty()) {
        c.model = model;
    }
    return c;
}

bool AiClientConfig::ready() const {
#ifdef HECQUIN_WITH_CURL
    return !api_key.empty();
#else
    (void)api_key;
    return false;
#endif
}

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
        Action a;
        a.kind = ActionKind::LocalDevice;
        a.transcript = n;
        std::string device = (target == "air") ? std::string("air conditioning") : target;
        a.reply = std::string("Okay, ") + verb + " the " + device + ".";
        return a;
    }

    if (std::regex_search(n, re_story)) {
        Action a;
        a.kind = ActionKind::InteractionTopicSearch;
        a.transcript = n;
        a.reply = "Sure — what kind of story would you like? I can search for a topic.";
        return a;
    }

    if (std::regex_search(n, re_music)) {
        Action a;
        a.kind = ActionKind::InteractionMusicSearch;
        a.transcript = n;
        a.reply = "Opening music search. What would you like to hear?";
        return a;
    }

    return std::nullopt;
}

Action CommandProcessor::call_external_api_(const std::string& user_text) const {
    Action a;
    a.kind = ActionKind::ExternalApi;
    a.transcript = user_text;

#ifndef HECQUIN_WITH_CURL
    a.reply = "External API is unavailable in this build (libcurl not found at configure time).";
    return a;
#else
    if (!config_.ready()) {
        a.reply = "Set OPENAI_API_KEY or HECQUIN_AI_API_KEY for cloud replies.";
        return a;
    }

    static std::once_flag curl_init_once;
    std::call_once(curl_init_once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

    const std::string body = std::string("{\"model\":\"") + json_escape(config_.model) +
                             "\",\"messages\":[{\"role\":\"user\",\"content\":\"" + json_escape(user_text) +
                             "\"}]}";

    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        a.reply = "Failed to initialize HTTP client.";
        return a;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth = "Authorization: Bearer " + config_.api_key;
    headers = curl_slist_append(headers, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, config_.chat_completions_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    const CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        a.reply = std::string("Request failed: ") + curl_easy_strerror(res);
        return a;
    }

    if (http_code < 200 || http_code >= 300) {
        a.reply = "API error HTTP " + std::to_string(http_code) + ": " + response.substr(0, 500);
        return a;
    }

    std::string content = extract_chat_content(response);
    if (content.empty()) {
        a.reply = "Could not parse model reply.";
        return a;
    }
    a.reply = content;
    return a;
#endif
}

Action CommandProcessor::process(const std::string& transcript) {
    const std::string trimmed = trim_copy(transcript);
    const std::string normalized = to_lower_copy(trimmed);
    if (normalized.empty()) {
        Action a;
        a.kind = ActionKind::None;
        a.reply = "(empty transcript)";
        return a;
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
