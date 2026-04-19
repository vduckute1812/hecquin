#include "learning/EnglishTutorProcessor.hpp"

#include "actions/GrammarCorrectionAction.hpp"
#include "ai/HttpClient.hpp"
#include "ai/OpenAiChatContent.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/LearningStore.hpp"
#include "learning/ProgressTracker.hpp"
#include "learning/RetrievalService.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <regex>
#include <sstream>

namespace hecquin::learning {

namespace {

std::string trim_copy(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
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
    std::ostringstream oss;
    oss << "{\"model\":\"" << json_escape(ai_.model) << "\","
        << "\"messages\":["
        << "{\"role\":\"system\",\"content\":\"" << json_escape(system) << "\"},"
        << "{\"role\":\"user\",\"content\":\"" << json_escape(user_text) << "\"}"
        << "]}";
    return oss.str();
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
    const auto result = http_post_json(ai_.chat_completions_url, ai_.api_key, body,
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
        err.explanation = "The tutor service returned an error (HTTP " +
                          std::to_string(result->status) + ").";
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
