// Unit tests for `ThinkingScheduler` — re-armable single-shot timer
// used by the listener to mask LLM latency with a soft earcon at +N ms.

#include "voice/ThinkingScheduler.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace {

int fail(const char* message) {
    std::cerr << "[test_thinking_scheduler] FAIL: " << message << std::endl;
    return 1;
}

using hecquin::voice::ThinkingScheduler;

} // namespace

int main() {
    // 1. Synchronous fast-path: delay <= 0 fires the callback before
    //    arm() returns.
    {
        ThinkingScheduler s;
        std::atomic<int> fired{0};
        s.arm(std::chrono::milliseconds(0),
              [&] { fired.fetch_add(1); });
        if (fired.load() != 1)
            return fail("delay==0 must fire synchronously");
        if (!s.fired())
            return fail("fired() must be true after sync fire");
    }

    // 2. Delayed fire: callback runs after delay elapses.
    {
        ThinkingScheduler s;
        std::atomic<int> fired{0};
        s.arm(std::chrono::milliseconds(20),
              [&] { fired.fetch_add(1); });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        if (fired.load() != 1)
            return fail("delayed timer should fire exactly once");
        if (!s.fired())
            return fail("fired() must reflect that the callback ran");
    }

    // 3. Cancel before fire: callback never runs.
    {
        ThinkingScheduler s;
        std::atomic<int> fired{0};
        s.arm(std::chrono::milliseconds(100),
              [&] { fired.fetch_add(1); });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        s.cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        if (fired.load() != 0)
            return fail("cancel before fire must suppress callback");
        if (s.fired())
            return fail("fired() must be false after cancel-before-fire");
    }

    // 4. Cancel after fire is a safe no-op.
    {
        ThinkingScheduler s;
        std::atomic<int> fired{0};
        s.arm(std::chrono::milliseconds(20),
              [&] { fired.fetch_add(1); });
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        if (fired.load() != 1)
            return fail("late fire");
        s.cancel(); // no-op
        if (fired.load() != 1)
            return fail("cancel after fire must not refire");
    }

    // 5. Re-arm before previous fired: only the second callback runs.
    {
        ThinkingScheduler s;
        std::atomic<int> first{0};
        std::atomic<int> second{0};
        s.arm(std::chrono::milliseconds(60),
              [&] { first.fetch_add(1); });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        s.arm(std::chrono::milliseconds(40),
              [&] { second.fetch_add(1); });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        if (first.load() != 0)
            return fail("re-arm should suppress the previous callback");
        if (second.load() != 1)
            return fail("re-arm should run the new callback");
    }

    // 6. Many arm/cancel cycles do not leak threads (worker is shared).
    {
        ThinkingScheduler s;
        std::atomic<int> fired{0};
        for (int i = 0; i < 50; ++i) {
            s.arm(std::chrono::milliseconds(50),
                  [&] { fired.fetch_add(1); });
            s.cancel();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        if (fired.load() != 0)
            return fail("rapid arm/cancel should never fire");
    }

    std::cout << "[test_thinking_scheduler] OK" << std::endl;
    return 0;
}
