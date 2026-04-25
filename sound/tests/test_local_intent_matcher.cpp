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
        if (!got || got->kind != ActionKind::MusicPlayback) {
            return fail("cancel music -> MusicPlayback");
        }
    }
    {
        const auto got = matcher.match("stop music");
        if (!got || got->kind != ActionKind::MusicPlayback) {
            return fail("stop music -> MusicPlayback");
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
