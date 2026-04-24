// Chain-of-Responsibility coverage for UtteranceRouter.  We stand up a real
// CommandProcessor backed by a fake IHttpClient so the only variable under
// test is the routing logic itself: local-intent short-circuit, drill /
// tutor dispatch by ListenerMode, and the final fallback into
// CommandProcessor::process.

#include "actions/Action.hpp"
#include "ai/CommandProcessor.hpp"
#include "ai/IHttpClient.hpp"
#include "config/ai/AiClientConfig.hpp"
#include "voice/UtteranceRouter.hpp"
#include "voice/VoiceListener.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

using hecquin::ai::IHttpClient;
using hecquin::voice::UtteranceRouter;

namespace {

int failures = 0;

void expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "[FAIL] " << label << std::endl;
        ++failures;
    }
}

// Canned chat response so `CommandProcessor::process` can resolve without
// touching the network.  Returns an ExternalApi reply with a distinct tag
// so the test can tell the fallback path apart from the direct callbacks.
class FakeHttp final : public IHttpClient {
public:
    std::optional<HttpResult> post_json(const std::string&, const std::string&,
                                        const std::string& body,
                                        long) override {
        last_body = body;
        return HttpResult{200,
            R"({"choices":[{"message":{"content":"chat-fallback-reply"}}]})"};
    }
    std::string last_body;
};

Utterance make_utt(const std::string& text) {
    Utterance u;
    u.transcript = text;
    return u;
}

} // namespace

int main() {
    FakeHttp http;
    AiClientConfig cfg;
    cfg.api_key = "fake";
    cfg.chat_completions_url = "https://unit.test/chat";
    CommandProcessor commands(cfg, http);

    // ------------------------------------------------------------------
    // 1. Local intent wins even when the listener is in Lesson mode.
    // ------------------------------------------------------------------
    {
        ListenerMode mode = ListenerMode::Lesson;
        int drill_calls = 0;
        int tutor_calls = 0;

        UtteranceRouter router(
            commands, mode,
            [&](const Utterance&) {
                ++drill_calls;
                Action a;
                a.kind = ActionKind::PronunciationFeedback;
                a.reply = "drill-handler";
                return a;
            },
            [&](const Utterance&) {
                ++tutor_calls;
                Action a;
                a.kind = ActionKind::EnglishLesson;
                a.reply = "tutor-handler";
                return a;
            });

        // "exit lesson" is a built-in local intent → LessonModeToggle.
        auto r = router.route(make_utt("exit lesson"));
        expect(r.from_local_intent, "local intent short-circuits chain");
        expect(r.action.kind == ActionKind::LessonModeToggle,
               "local intent dispatches to LessonModeToggle");
        expect(drill_calls == 0 && tutor_calls == 0,
               "local match never consults mode callbacks");
    }

    // ------------------------------------------------------------------
    // 2. Drill mode forwards to the drill callback.
    // ------------------------------------------------------------------
    {
        ListenerMode mode = ListenerMode::Drill;
        int drill_calls = 0;
        int tutor_calls = 0;

        UtteranceRouter router(
            commands, mode,
            [&](const Utterance&) {
                ++drill_calls;
                Action a;
                a.kind = ActionKind::PronunciationFeedback;
                a.reply = "drill-handler";
                return a;
            },
            [&](const Utterance&) {
                ++tutor_calls;
                Action a;
                a.kind = ActionKind::EnglishLesson;
                a.reply = "tutor-handler";
                return a;
            });

        auto r = router.route(make_utt("the rain in spain"));
        expect(!r.from_local_intent, "non-intent utterance is not local");
        expect(drill_calls == 1 && tutor_calls == 0,
               "Drill mode reaches drill callback only");
        expect(r.action.reply == "drill-handler",
               "drill callback action is returned");
    }

    // ------------------------------------------------------------------
    // 3. Lesson mode forwards to the tutor callback.
    // ------------------------------------------------------------------
    {
        ListenerMode mode = ListenerMode::Lesson;
        int drill_calls = 0;
        int tutor_calls = 0;

        UtteranceRouter router(
            commands, mode,
            [&](const Utterance&) { ++drill_calls; return Action{}; },
            [&](const Utterance&) {
                ++tutor_calls;
                Action a;
                a.kind = ActionKind::EnglishLesson;
                a.reply = "tutor-handler";
                return a;
            });

        auto r = router.route(make_utt("please correct my grammar"));
        expect(drill_calls == 0 && tutor_calls == 1,
               "Lesson mode reaches tutor callback only");
        expect(r.action.reply == "tutor-handler",
               "tutor callback action is returned");
    }

    // ------------------------------------------------------------------
    // 4. Assistant mode falls through to CommandProcessor::process.
    // ------------------------------------------------------------------
    {
        ListenerMode mode = ListenerMode::Assistant;
        int drill_calls = 0;
        int tutor_calls = 0;

        UtteranceRouter router(
            commands, mode,
            [&](const Utterance&) { ++drill_calls; return Action{}; },
            [&](const Utterance&) { ++tutor_calls; return Action{}; });

        auto r = router.route(make_utt("what's the weather today"));
        expect(!r.from_local_intent, "fallback path reports non-local");
        expect(drill_calls == 0 && tutor_calls == 0,
               "Assistant mode skips both mode callbacks");
        expect(r.action.kind == ActionKind::ExternalApi,
               "fallback returns ExternalApi action from chat");
        expect(r.action.reply == "chat-fallback-reply",
               "fallback carries chat-client reply through");
    }

    // ------------------------------------------------------------------
    // 5. Missing mode callback still falls through cleanly.
    // ------------------------------------------------------------------
    {
        ListenerMode mode = ListenerMode::Drill;
        UtteranceRouter router(commands, mode, nullptr, nullptr);
        auto r = router.route(make_utt("no handler available"));
        expect(r.action.kind == ActionKind::ExternalApi,
               "null drill callback falls through to chat fallback");
    }

    if (failures == 0) {
        std::cout << "[test_utterance_router] all assertions passed" << std::endl;
        return 0;
    }
    return 1;
}
