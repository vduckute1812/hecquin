// StreamingSdlPlayer gain-stage unit test.
//
// Drives the static `apply_gain` helper directly so we can validate
// the per-sample slewing without opening an SDL audio device:
//   - Unity gain leaves samples untouched.
//   - Target = 0.5 with ramp_ms = 0 still scales (instant convergence
//     within the first buffer).
//   - Long ramps converge gradually and never overshoot.
//   - Saturation clips at int16 limits without UB.
//   - Calling apply_gain across multiple buffers preserves continuity.

#include "tts/playback/StreamingSdlPlayer.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "[FAIL] " << label << std::endl;
        ++failures;
    }
}

constexpr int kSampleRate = 22050;

std::vector<std::int16_t> make_dc(std::size_t n, std::int16_t v) {
    return std::vector<std::int16_t>(n, v);
}

} // namespace

int main() {
    using hecquin::tts::playback::StreamingSdlPlayer;

    {
        // Unity in / unity out: fast path, no scaling.
        auto buf = make_dc(64, 1000);
        float cur = 1.0f, step = 0.0f, last_target = 1.0f;
        StreamingSdlPlayer::apply_gain(buf.data(), buf.size(), kSampleRate,
                                       /*target=*/1.0f, /*ramp_ms=*/0,
                                       cur, step, last_target);
        for (auto s : buf) expect(s == 1000, "unity gain leaves samples untouched");
    }

    {
        // Snap to half: with ramp_ms=0 the helper plans the step over
        // the buffer length so the last sample equals the target.
        auto buf = make_dc(8, 10000);
        float cur = 1.0f, step = 0.0f, last_target = 1.0f;
        StreamingSdlPlayer::apply_gain(buf.data(), buf.size(), kSampleRate,
                                       /*target=*/0.5f, /*ramp_ms=*/0,
                                       cur, step, last_target);
        // The first sample is still at near-unity, the last at ~0.5.
        expect(buf.front() > 8000, "first sample close to unity gain");
        expect(buf.back()  < 6000, "final sample close to 0.5 gain");
        expect(std::abs(cur - 0.5f) < 1e-5f,
               "current gain converges to target after one buffer");
    }

    {
        // Long ramp: 100 ms at 22050 Hz = 2205 samples, but we only
        // process 256 samples.  Step magnitude per sample is small;
        // current_gain should be close to 1.0 still.
        auto buf = make_dc(256, 10000);
        float cur = 1.0f, step = 0.0f, last_target = 1.0f;
        StreamingSdlPlayer::apply_gain(buf.data(), buf.size(), kSampleRate,
                                       /*target=*/0.0f, /*ramp_ms=*/100,
                                       cur, step, last_target);
        // After 256 samples of a 2205-sample ramp the gain dropped by
        // about 256/2205 ≈ 0.116, so current ≈ 0.884.
        expect(cur > 0.85f && cur < 0.92f,
               "long ramp progresses gradually (mid-ramp gain in expected band)");
        // No overshoot below target.
        expect(cur > 0.0f, "ramp does not overshoot below target");
    }

    {
        // Multi-buffer continuity: two back-to-back calls with the
        // same target eventually reach it without overshoot.
        float cur = 1.0f, step = 0.0f, last_target = 1.0f;
        const float target = 0.25f;
        const int ramp_ms = 10; // 220 samples at 22050
        for (int i = 0; i < 20; ++i) {
            auto buf = make_dc(64, 5000);
            StreamingSdlPlayer::apply_gain(buf.data(), buf.size(),
                                           kSampleRate, target, ramp_ms,
                                           cur, step, last_target);
        }
        expect(std::abs(cur - target) < 1e-4f,
               "multi-buffer ramp converges exactly to target");
        expect(step == 0.0f, "ramp_step zeroed on convergence");
    }

    {
        // Saturation: 1.5x boost on max-positive sample clips at INT16_MAX
        // (no UB / overflow).
        auto buf = make_dc(4, 30000);
        float cur = 1.5f, step = 0.0f, last_target = 1.5f;
        StreamingSdlPlayer::apply_gain(buf.data(), buf.size(), kSampleRate,
                                       /*target=*/1.5f, /*ramp_ms=*/0,
                                       cur, step, last_target);
        for (auto s : buf) expect(s == 32767, "1.5x boost saturates at INT16_MAX");
    }

    {
        // Mid-ramp target change retunes smoothly without thrashing.
        float cur = 1.0f, step = 0.0f, last_target = 1.0f;
        auto buf = make_dc(128, 10000);
        StreamingSdlPlayer::apply_gain(buf.data(), buf.size(), kSampleRate,
                                       /*target=*/0.25f, /*ramp_ms=*/50,
                                       cur, step, last_target);
        const float mid = cur;
        expect(mid > 0.25f, "mid-ramp gain still above target");
        // Now retarget to 1.0; next buffer should ramp back up.
        auto buf2 = make_dc(128, 10000);
        StreamingSdlPlayer::apply_gain(buf2.data(), buf2.size(), kSampleRate,
                                       /*target=*/1.0f, /*ramp_ms=*/50,
                                       cur, step, last_target);
        expect(cur > mid, "retarget to 1.0 starts ramping up");
    }

    if (failures == 0) {
        std::cout << "[test_streaming_player_gain] all assertions passed"
                  << std::endl;
        return 0;
    }
    return 1;
}
