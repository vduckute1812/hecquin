// Unit tests for WakeWordGate — the post-Whisper / pre-router decision
// that gates which transcripts reach the rest of the pipeline.  Drives
// the gate with synthetic transcripts and a moveable Clock::time_point
// so the tests stay deterministic and offline.

#include "voice/WakeWordGate.hpp"

#include <chrono>
#include <iostream>
#include <string>

namespace {

int fail(const char* message) {
    std::cerr << "[test_wake_word_gate] FAIL: " << message << std::endl;
    return 1;
}

using hecquin::voice::WakeWordGate;

WakeWordGate::Clock::time_point t0() {
    return WakeWordGate::Clock::time_point{}; // deterministic epoch
}

WakeWordGate::Clock::time_point at_ms(long long ms) {
    return t0() + std::chrono::milliseconds(ms);
}

} // namespace

int main() {
    using Mode = WakeWordGate::Mode;

    // 1. Always mode: every transcript routes unchanged.
    {
        WakeWordGate g;
        g.set_mode(Mode::Always);
        const auto d = g.decide("turn the lights on", t0());
        if (!d.route)                       return fail("Always: should route");
        if (d.transcript != "turn the lights on")
            return fail("Always: must not strip transcript");
    }

    // 2. Wake-word mode: a transcript with the wake phrase routes and
    //    the wake phrase is stripped before the router sees it.
    {
        WakeWordGate g;
        g.set_mode(Mode::WakeWord);
        const auto d = g.decide("hey hecquin, what's the weather", t0());
        if (!d.route)                       return fail("WakeWord: prefixed should route");
        if (d.transcript != "what's the weather")
            return fail("WakeWord: should strip wake phrase");
    }

    // 3. Wake-word mode: an unprefixed transcript outside the window
    //    is dropped; once a wake phrase fires, follow-ups within the
    //    window route without re-saying the phrase.
    {
        WakeWordGate g;
        g.set_mode(Mode::WakeWord);

        const auto miss = g.decide("turn the lights on", at_ms(0));
        if (miss.route)                     return fail("WakeWord: cold transcript must skip");

        const auto wake = g.decide("hecquin set a timer for ten minutes", at_ms(100));
        if (!wake.route)                    return fail("WakeWord: wake phrase must route");
        if (wake.transcript != "set a timer for ten minutes")
            return fail("WakeWord: wake-strip must remove the phrase");

        const auto follow_up = g.decide("and turn off the lights", at_ms(2000));
        if (!follow_up.route)
            return fail("WakeWord: follow-up within window must route");

        // Outside the default 8 s window: drop again.
        const auto cold = g.decide("forget what I said", at_ms(20000));
        if (cold.route)
            return fail("WakeWord: out-of-window transcript must skip");
    }

    // 4. PTT mode: pressed routes, released drops; mode change is
    //    independent of the wake regex.
    {
        WakeWordGate g;
        g.set_mode(Mode::Ptt);
        if (g.decide("any text", t0()).route)
            return fail("PTT released should drop");
        g.set_ptt_pressed(true);
        if (!g.decide("any text", t0()).route)
            return fail("PTT pressed should route");
        g.set_ptt_pressed(false);
        if (g.decide("any text", t0()).route)
            return fail("PTT released after press should drop");
    }

    // 5. set_wake_pattern: a valid pattern installs cleanly and is
    //    visible via wake_pattern().
    {
        WakeWordGate g;
        if (!g.set_wake_pattern(R"(^\s*computer[,\s])"))
            return fail("valid pattern must install");
        if (g.wake_pattern() != R"(^\s*computer[,\s])")
            return fail("wake_pattern() round-trip");

        g.set_mode(Mode::WakeWord);
        const auto d = g.decide("computer, set a reminder", t0());
        if (!d.route)                       return fail("custom pattern: should route");
        if (d.transcript != "set a reminder")
            return fail("custom pattern: should strip");
    }

    // 6. set_wake_pattern: invalid regex must report failure and leave
    //    the previous pattern intact (gate keeps working with the old
    //    phrase rather than crashing or going silent).
    {
        WakeWordGate g;
        if (!g.set_wake_pattern(R"(^\s*computer[,\s])"))
            return fail("baseline pattern install");
        const std::string before = g.wake_pattern();

        if (g.set_wake_pattern("(["))
            return fail("malformed regex must NOT install");
        if (g.wake_pattern() != before)
            return fail("malformed regex must leave old pattern in place");

        // And the gate must still route follow-ups under the old pattern.
        g.set_mode(Mode::WakeWord);
        const auto d = g.decide("computer set a timer", t0());
        if (!d.route)
            return fail("after rejected install, old pattern should still match");
    }

    // 7. The wake phrase must only match at the *start* of the transcript
    //    so a stray name inside a longer sentence does not open the window.
    {
        WakeWordGate g;
        g.set_mode(Mode::WakeWord);
        const auto d = g.decide("the assistant called hecquin is ready", t0());
        if (d.route)
            return fail("wake phrase mid-utterance must not open window");
    }

    std::cout << "[test_wake_word_gate] OK" << std::endl;
    return 0;
}
