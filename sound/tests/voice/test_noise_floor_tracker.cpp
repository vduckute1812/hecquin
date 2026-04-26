// Unit tests for NoiseFloorTracker — the pure component that powers the
// adaptive VAD in UtteranceCollector.  Drives the tracker with synthetic
// frame RMS values so the tests stay deterministic and offline.

#include "voice/NoiseFloorTracker.hpp"

#include <cmath>
#include <iostream>

namespace {

int fail(const char* message) {
    std::cerr << "[test_noise_floor_tracker] FAIL: " << message << std::endl;
    return 1;
}

bool approx(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

hecquin::voice::NoiseFloorConfig small_cfg(int n = 5) {
    hecquin::voice::NoiseFloorConfig c;
    c.calibration_samples = n;
    c.ema_alpha = 0.5f;          // fast convergence so tests stay short
    c.seed_floor = 0.005f;
    c.min_floor = 1e-4f;
    c.max_floor = 1.0f;
    return c;
}

} // namespace

int main() {
    using hecquin::voice::NoiseFloorTracker;

    // 1. Calibration uses the median, not the mean.  A single outlier
    //    must not anchor the floor far above the actual ambient level.
    {
        NoiseFloorTracker t(small_cfg(5));
        t.observe(0.001f, false);
        t.observe(0.0012f, false);
        t.observe(0.0015f, false);
        t.observe(0.001f, false);
        t.observe(0.5f, false);   // outlier — a cough
        // Sorted: [0.001, 0.001, 0.0012, 0.0015, 0.5] → median = 0.0012.
        if (!t.calibrated())                 return fail("should be calibrated after N samples");
        if (!approx(t.floor(), 0.0012f))     return fail("median floor ignores outlier");
    }

    // 2. Speech (collecting=true) is ignored entirely so it can't pull
    //    the floor up during calibration.
    {
        NoiseFloorTracker t(small_cfg(3));
        t.observe(0.5f, true);   // speech, dropped
        t.observe(0.5f, true);   // speech, dropped
        t.observe(0.001f, false);
        t.observe(0.001f, false);
        t.observe(0.001f, false);
        if (!t.calibrated())                 return fail("speech-skip path should still calibrate");
        if (!approx(t.floor(), 0.001f))      return fail("speech must not influence floor");
    }

    // 3. EMA after calibration converges toward the new ambient level.
    //    With alpha=0.5 the floor should be ~halfway after one step.
    {
        NoiseFloorTracker t(small_cfg(3));
        t.observe(0.001f, false);
        t.observe(0.001f, false);
        t.observe(0.001f, false); // floor = 0.001
        t.observe(0.011f, false); // EMA: 0.5*0.001 + 0.5*0.011 = 0.006
        if (!approx(t.floor(), 0.006f))      return fail("EMA should land midway with alpha=0.5");
    }

    // 4. Speech in the adaptation phase is also ignored; floor stays put.
    {
        NoiseFloorTracker t(small_cfg(3));
        t.observe(0.002f, false);
        t.observe(0.002f, false);
        t.observe(0.002f, false); // floor = 0.002
        const float before = t.floor();
        t.observe(0.5f, true);    // speech in the room
        t.observe(0.5f, true);
        if (!approx(t.floor(), before))      return fail("speech must not move EMA floor");
    }

    // 5. Clamps engage at both ends so derived thresholds can't collapse
    //    to zero or run away to one.
    {
        auto cfg = small_cfg(3);
        cfg.min_floor = 0.005f;
        cfg.max_floor = 0.05f;
        NoiseFloorTracker t(cfg);
        t.observe(0.0f, false);
        t.observe(0.0f, false);
        t.observe(0.0f, false);
        if (t.floor() < cfg.min_floor)       return fail("floor below min clamp");

        // Saturate at the top: a long burst of loud frames must not
        // exceed `max_floor`.
        for (int i = 0; i < 200; ++i) t.observe(10.0f, false);
        if (t.floor() > cfg.max_floor + 1e-6f) return fail("floor above max clamp");
    }

    // 6. Reset clears state — useful for re-calibrating after a device change.
    {
        NoiseFloorTracker t(small_cfg(3));
        t.observe(0.01f, false);
        t.observe(0.01f, false);
        t.observe(0.01f, false);
        if (!t.calibrated())                 return fail("pre-reset must be calibrated");
        t.reset();
        if (t.calibrated())                  return fail("reset should clear calibration");
        if (!approx(t.floor(), 0.005f))      return fail("reset should restore seed floor");
    }

    // 7. Defensive: NaN / negative inputs are dropped silently.
    {
        NoiseFloorTracker t(small_cfg(3));
        t.observe(std::nanf(""), false);
        t.observe(-0.1f, false);
        if (t.calibration_samples_collected() != 0)
            return fail("non-finite / negative inputs must be discarded");
    }

    std::cout << "[test_noise_floor_tracker] OK" << std::endl;
    return 0;
}
