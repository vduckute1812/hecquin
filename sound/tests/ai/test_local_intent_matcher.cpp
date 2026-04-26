#include "actions/ActionKind.hpp"
#include "ai/LocalIntentMatcher.hpp"

#include <iostream>
#include <string>

namespace {

int fail(const char* message) {
    std::cerr << "[test_local_intent_matcher] FAIL: " << message << std::endl;
    return 1;
}

} // namespace

int main() {
    hecquin::ai::LocalIntentMatcher matcher;

    {
        const auto got = matcher.match("turn on air conditioner please");
        if (!got || got->kind != ActionKind::LocalDevice) {
            return fail("turn on air -> LocalDevice");
        }
    }
    {
        const auto got = matcher.match("TURN OFF SWITCH now");
        if (!got || got->kind != ActionKind::LocalDevice) {
            return fail("turn off switch (uppercase) -> LocalDevice");
        }
    }
    {
        const auto got = matcher.match("please tell me a story about dragons");
        if (!got || got->kind != ActionKind::InteractionTopicSearch) {
            return fail("tell me a story -> InteractionTopicSearch");
        }
    }
    {
        const auto got = matcher.match("open music now");
        if (!got || got->kind != ActionKind::MusicSearchPrompt) {
            return fail("open music -> MusicSearchPrompt");
        }
    }
    {
        const auto got = matcher.match("cancel music please");
        if (!got || got->kind != ActionKind::MusicCancel) {
            return fail("cancel music -> MusicCancel");
        }
    }
    {
        const auto got = matcher.match("stop music");
        if (!got || got->kind != ActionKind::MusicCancel) {
            return fail("stop music -> MusicCancel");
        }
    }
    {
        const auto got = matcher.match("end music now");
        if (!got || got->kind != ActionKind::MusicCancel) {
            return fail("end music -> MusicCancel");
        }
    }
    {
        const auto got = matcher.match("pause music");
        if (!got || got->kind != ActionKind::MusicPause) {
            return fail("pause music -> MusicPause");
        }
    }
    {
        const auto got = matcher.match("continue music please");
        if (!got || got->kind != ActionKind::MusicResume) {
            return fail("continue music -> MusicResume");
        }
    }
    {
        const auto got = matcher.match("resume music");
        if (!got || got->kind != ActionKind::MusicResume) {
            return fail("resume music -> MusicResume");
        }
    }
    {
        // The pause/resume patterns must not steal "open music" — that
        // still has to enter the search prompt flow.
        const auto got = matcher.match("open music now");
        if (!got || got->kind != ActionKind::MusicSearchPrompt) {
            return fail("open music -> MusicSearchPrompt (post pause/resume)");
        }
    }
    {
        const auto got = matcher.match("start english lesson");
        if (!got || got->kind != ActionKind::LessonModeToggle) {
            return fail("start english lesson -> LessonModeToggle");
        }
    }
    {
        const auto got = matcher.match("exit lesson");
        if (!got || got->kind != ActionKind::LessonModeToggle) {
            return fail("exit lesson -> LessonModeToggle");
        }
    }
    {
        const auto got = matcher.match("   ");
        if (got) {
            return fail("empty transcript -> nullopt");
        }
    }
    {
        const auto got = matcher.match("what is the capital of France");
        if (got) {
            return fail("free-form question -> nullopt (passthrough to LLM)");
        }
    }
    return 0;
}
