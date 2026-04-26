#include "ai/OpenAiChatContent.hpp"

#include <cstdlib>
#include <optional>
#include <string>

namespace {

int fail(const char* /*message*/) {
    return 1;
}

} // namespace

int main() {
    {
        const std::string json = R"({"choices":[{"message":{"role":"assistant","content":"Hi"}}]})";
        const auto got = extract_openai_chat_assistant_content(json);
        if (!got || *got != "Hi") {
            return fail("compact choices.message.content");
        }
    }
    {
        const std::string json = "{\"choices\":[{\"message\":{  \"content\"  :  \"ok\" }}]}";
        const auto got = extract_openai_chat_assistant_content(json);
        if (!got || *got != "ok") {
            return fail("whitespace around content key");
        }
    }
    {
        const std::string json = R"({"choices":[{"message":{"content":null}}]})";
        const auto got = extract_openai_chat_assistant_content(json);
        if (!got || !got->empty()) {
            return fail("null content should yield empty string");
        }
    }
    {
        const std::string json = R"({"choices":[{"message":{"content":""}}]})";
        const auto got = extract_openai_chat_assistant_content(json);
        if (!got || !got->empty()) {
            return fail("empty string content");
        }
    }
    {
        const std::string json = R"({"choices":[{"message":{"content":"line\n2"}}]})";
        const auto got = extract_openai_chat_assistant_content(json);
        if (!got || *got != "line\n2") {
            return fail("escaped newline");
        }
    }
    {
        const std::string json = R"({"foo":1})";
        const auto got = extract_openai_chat_assistant_content(json);
        if (got) {
            return fail("missing content should be nullopt");
        }
    }
    {
        const std::string json = R"({"choices":[{"message":{"content":"\u0041"}}]})";
        const auto got = extract_openai_chat_assistant_content(json);
        if (!got || *got != "A") {
            return fail("basic \\uXXXX");
        }
    }
    return 0;
}
