// `process` vs `process_async` parity (local + chat paths).

#include "actions/Action.hpp"
#include "actions/ActionKind.hpp"
#include "ai/CommandProcessor.hpp"
#include "ai/IHttpClient.hpp"
#include "config/ai/AiClientConfig.hpp"

#include <iostream>
#include <optional>
#include <string>

using hecquin::ai::HttpResult;
using hecquin::ai::IHttpClient;

namespace {

int failures = 0;

void expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "[FAIL] " << label << std::endl;
        ++failures;
    }
}

bool same_action_shape(const Action& a, const Action& b) {
    return a.kind == b.kind && a.reply == b.reply && a.transcript == b.transcript &&
           a.enable == b.enable && a.param == b.param;
}

class FakeHttp final : public IHttpClient {
public:
    std::optional<HttpResult> post_json(const std::string&, const std::string&,
                                        const std::string&, long) override {
        return HttpResult{200,
            R"({"choices":[{"message":{"content":"async-test-reply"}}]})"};
    }
};

} // namespace

int main() {
    FakeHttp http;
    AiClientConfig cfg;
    cfg.api_key = "fake";
    cfg.chat_completions_url = "https://unit.test/chat";
    CommandProcessor commands(cfg, http);

    // Chat fallback: process_async().get() matches process().
    {
        const Action sync = commands.process("hello there");
        const Action async = commands.process_async("hello there").get();
        expect(same_action_shape(sync, async),
               "process and process_async agree on chat fallback");
        expect(sync.kind == ActionKind::ExternalApi, "fallback is ExternalApi");
    }

    // Local intent: both paths short-circuit HTTP identically.
    {
        const Action sync = commands.process("open music");
        const Action async = commands.process_async("open music").get();
        expect(same_action_shape(sync, async),
               "process and process_async agree on local intent");
        expect(sync.kind == ActionKind::MusicSearchPrompt, "open music is local");
    }

    return failures > 0 ? 1 : 0;
}
