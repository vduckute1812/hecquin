#include "learning/tutor/TutorChatRequest.hpp"

#include <nlohmann/json.hpp>

namespace hecquin::learning::tutor {

std::string build_chat_body(const AiClientConfig& ai,
                            const std::string& user_text,
                            const std::string& context) {
    std::string system = ai.tutor_system_prompt;
    if (!context.empty()) {
        system += "\n\nReference snippets (use only if relevant):\n" + context;
    }
    const nlohmann::json body = {
        {"model", ai.model},
        {"messages", nlohmann::json::array({
            {{"role", "system"}, {"content", system}},
            {{"role", "user"},   {"content", user_text}},
        })},
    };
    return body.dump(-1, ' ', false,
                     nlohmann::json::error_handler_t::replace);
}

} // namespace hecquin::learning::tutor
