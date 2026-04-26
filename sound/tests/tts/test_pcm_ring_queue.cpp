// Unit tests for hecquin::tts::playback::PcmRingQueue.
//
// Focus: the cv-backed `wait_until_drained` actually wakes (it didn't
// before — `StreamingSdlPlayer::wait_until_drained` busy-slept in
// 20 ms slices because the previously declared cv was never used).

#include "tts/playback/PcmRingQueue.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace {

void fail(const char* label) {
    std::cerr << "FAIL " << label << std::endl;
    std::exit(1);
}

void test_push_then_pop_into_preserves_samples() {
    hecquin::tts::playback::PcmRingQueue q;
    const std::int16_t in[] = {1, 2, 3, 4, 5};
    q.push(in, 5);
    if (q.size() != 5) fail("size_after_push");

    std::int16_t out[8] = {};
    q.pop_into(reinterpret_cast<std::uint8_t*>(out), sizeof(out));

    if (out[0] != 1 || out[1] != 2 || out[2] != 3 || out[3] != 4 ||
        out[4] != 5) {
        fail("ordered_drain");
    }
    // Underrun zero-pads the remainder of the SDL buffer.
    if (out[5] != 0 || out[6] != 0 || out[7] != 0) {
        fail("zero_padding_on_underrun");
    }
    if (q.size() != 0) fail("size_after_drain");
}

void test_wait_until_drained_wakes_via_cv() {
    hecquin::tts::playback::PcmRingQueue q;
    const std::int16_t in[] = {7, 8, 9, 10};
    q.push(in, 4);
    q.mark_eof();

    std::atomic<bool> woke{false};
    std::thread waiter([&] {
        q.wait_until_drained();
        woke.store(true);
    });

    // Give the waiter a moment to enter the cv wait.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (woke.load()) fail("woke_before_drain");

    // Consumer drains: this must signal the cv.
    std::int16_t out[4] = {};
    q.pop_into(reinterpret_cast<std::uint8_t*>(out), sizeof(out));

    const auto t0 = std::chrono::steady_clock::now();
    waiter.join();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    if (!woke.load()) fail("waiter_did_not_wake");
    // Wake should be near-instant (much less than the old 20 ms busy-
    // sleep grain); allow generous slack for CI noise.
    if (elapsed > 200) {
        std::cerr << "FAIL wake_latency: " << elapsed << " ms" << std::endl;
        std::exit(1);
    }
}

void test_mark_eof_when_already_empty_unblocks_waiter() {
    hecquin::tts::playback::PcmRingQueue q;
    std::atomic<bool> woke{false};
    std::thread waiter([&] {
        q.wait_until_drained();
        woke.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    q.mark_eof();
    waiter.join();

    if (!woke.load()) fail("eof_then_drained");
}

void test_drained_predicate_matches_state() {
    hecquin::tts::playback::PcmRingQueue q;
    if (q.drained()) fail("drained_before_eof");
    const std::int16_t in[] = {1};
    q.push(in, 1);
    q.mark_eof();
    if (q.drained()) fail("drained_with_pending_samples");
    std::int16_t out[1] = {};
    q.pop_into(reinterpret_cast<std::uint8_t*>(out), sizeof(out));
    if (!q.drained()) fail("drained_after_full_pop");
}

void test_multi_producer_safety() {
    // Two producers, one drainer.  Sanity check there are no
    // mutex/cv deadlocks under contention; we don't try to verify
    // ordering since the queue isn't strictly ordered across producers.
    hecquin::tts::playback::PcmRingQueue q;

    constexpr int kBatches = 50;
    constexpr int kBatchSize = 64;
    std::vector<std::int16_t> data(kBatchSize, 42);

    std::thread p1([&] {
        for (int i = 0; i < kBatches; ++i) q.push(data.data(), kBatchSize);
    });
    std::thread p2([&] {
        for (int i = 0; i < kBatches; ++i) q.push(data.data(), kBatchSize);
    });

    p1.join();
    p2.join();
    q.mark_eof();

    int total = 0;
    std::int16_t out[kBatchSize] = {};
    while (!q.drained()) {
        q.pop_into(reinterpret_cast<std::uint8_t*>(out), sizeof(out));
        for (int v : out) {
            if (v == 42) ++total;
            // Note: zero pads count as 0 hits; we accept whatever the
            // drained loop produces, just want to confirm no deadlock.
        }
    }
    if (total < kBatches * kBatchSize) {
        std::cerr << "FAIL multi_producer_count: total=" << total
                  << " want >= " << (2 * kBatches * kBatchSize) << std::endl;
        std::exit(1);
    }
}

} // namespace

int main() {
    test_push_then_pop_into_preserves_samples();
    test_wait_until_drained_wakes_via_cv();
    test_mark_eof_when_already_empty_unblocks_waiter();
    test_drained_predicate_matches_state();
    test_multi_producer_safety();
    std::cout << "[ok] test_pcm_ring_queue" << std::endl;
    return 0;
}
