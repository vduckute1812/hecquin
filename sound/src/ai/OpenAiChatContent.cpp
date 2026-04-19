#include "ai/OpenAiChatContent.hpp"

#include <nlohmann/json.hpp>

std::optional<std::string> extract_openai_chat_assistant_content(const std::string& body) {
    try {
        const auto parsed = nlohmann::json::parse(body, /*cb=*/nullptr, /*allow_exceptions=*/true,
                                                   /*ignore_comments=*/true);
        const auto& choices = parsed.at("choices");
        if (!choices.is_array() || choices.empty()) {
            return std::nullopt;
        }
        const auto& message = choices[0].at("message");
        const auto it = message.find("content");
        if (it == message.end()) return std::nullopt;
        if (it->is_null())       return std::string{};
        if (!it->is_string())    return std::nullopt;
        return it->get<std::string>();
    } catch (...) {
        return std::nullopt;
    }
}
