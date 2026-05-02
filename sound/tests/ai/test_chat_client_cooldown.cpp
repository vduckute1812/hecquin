// Unit tests for ChatClient's cooldown logic.  Drives the client with
// a scripted IHttpClient and asserts:
//   - consecutive 5xx / transport failures engage the cooldown,
//   - while in cooldown, ask() short-circuits without invoking HTTP,
//   - a successful response resets the failure counter,
//   - 4xx responses do NOT count toward the cooldown threshold
//     (they aren't going to fix themselves by waiting).

#include "ai/ChatClient.hpp"
#include "ai/IHttpClient.hpp"
#include "config/ai/AiClientConfig.hpp"

#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <optional>
#include <string>

namespace {

using hecquin::ai::ChatClient;
using hecquin::ai::HttpResult;
using hecquin::ai::IHttpClient;

int fail(const char* message) {
    std::cerr << "[test_chat_client_cooldown] FAIL: " << message << std::endl;
    return 1;
}

class ScriptedHttp final : public IHttpClient {
public:
    std::deque<std::optional<HttpResult>> canned;
    int calls = 0;

    std::optional<HttpResult> post_json(const std::string&, const std::string&,
                                        const std::string&, long) override {
        ++calls;
        if (canned.empty()) return std::nullopt;
        auto v = canned.front();
        canned.pop_front();
        return v;
    }
};

AiClientConfig make_cfg() {
    AiClientConfig c;
    c.api_key = "fake";
    c.chat_completions_url = "https://unit.test/v1/chat/completions";
    c.system_prompt = "you are tested.";
    return c;
}

const char* good_body() {
    return R"({"choices":[{"message":{"content":"hi there"}}]})";
}

} // namespace

int main() {
    // The cooldown env knobs are read in the constructor.  Wipe them so
    // tests run with the documented defaults regardless of operator env.
    ::unsetenv("HECQUIN_CHAT_COOLDOWN_FAILURES");
    ::unsetenv("HECQUIN_CHAT_COOLDOWN_WINDOW_MS");
    ::unsetenv("HECQUIN_CHAT_COOLDOWN_DURATION_MS");

    using Clock = ChatClient::Clock;
    const auto far_future = Clock::now() + std::chrono::seconds(600);

    // 1. Two consecutive 5xx failures engage the cooldown; a third
    //    request short-circuits without an HTTP call.
    {
        ScriptedHttp http;
        http.canned.push_back(HttpResult{500, "boom"});
        http.canned.push_back(HttpResult{503, "boom"});
        ChatClient client(make_cfg(), http);

        client.ask("hello");
        client.ask("are you there");
        if (http.calls != 2)
            return fail("expected exactly 2 HTTP calls before cooldown");
        if (!client.in_cooldown(Clock::now()))
            return fail("two 5xx failures should engage cooldown");

        // While in cooldown, no HTTP call should be made.
        const int calls_before = http.calls;
        const auto a = client.ask("third call during cooldown");
        if (http.calls != calls_before)
            return fail("ask() during cooldown must not invoke HTTP");
        if (a.reply.empty())
            return fail("cooldown reply must not be empty");
    }

    // 2. Transport failures (nullopt) also engage the cooldown.
    {
        ScriptedHttp http;
        http.canned.push_back(std::nullopt);
        http.canned.push_back(std::nullopt);
        ChatClient client(make_cfg(), http);

        client.ask("a");
        client.ask("b");
        if (!client.in_cooldown(Clock::now()))
            return fail("two transport failures should engage cooldown");
    }

    // 3. 4xx responses do NOT count toward cooldown — they signal a
    //    user / config problem that won't fix itself by waiting.
    {
        ScriptedHttp http;
        for (int i = 0; i < 5; ++i) {
            http.canned.push_back(HttpResult{401, "unauthorized"});
        }
        ChatClient client(make_cfg(), http);
        for (int i = 0; i < 5; ++i) {
            client.ask("fail");
        }
        if (client.in_cooldown(far_future))
            return fail("repeated 401s must not engage cooldown");
        if (http.calls != 5)
            return fail("4xx responses should not skip HTTP");
    }

    // 4. A success resets the failure counter so a single later
    //    failure cannot trip the cooldown.
    {
        ScriptedHttp http;
        http.canned.push_back(HttpResult{500, "x"});
        http.canned.push_back(HttpResult{200, good_body()});
        http.canned.push_back(HttpResult{500, "y"});
        ChatClient client(make_cfg(), http);

        client.ask("first 500");
        client.ask("then 200 (resets)");
        client.ask("single 500 after reset");
        if (client.in_cooldown(Clock::now()))
            return fail("a success should reset the failure counter");
    }

    // 5. Custom env override: HECQUIN_CHAT_COOLDOWN_FAILURES=1 makes
    //    the cooldown engage after a single failure.
    {
        ::setenv("HECQUIN_CHAT_COOLDOWN_FAILURES", "1", 1);
        ScriptedHttp http;
        http.canned.push_back(HttpResult{500, "x"});
        ChatClient client(make_cfg(), http);
        client.ask("one strike");
        const bool engaged = client.in_cooldown(Clock::now());
        ::unsetenv("HECQUIN_CHAT_COOLDOWN_FAILURES");
        if (!engaged)
            return fail("threshold=1 should engage cooldown after one 500");
    }

    std::cout << "[test_chat_client_cooldown] OK" << std::endl;
    return 0;
}
