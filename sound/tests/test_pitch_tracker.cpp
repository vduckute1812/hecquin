#include "learning/prosody/PitchTracker.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace hecquin::learning::prosody;

namespace {

int fail(const char* msg) {
    std::cerr << "[test_pitch_tracker] FAIL: " << msg << std::endl;
    return 1;
}

std::vector<float> sine_wave(float freq_hz, float duration_s, int sample_rate) {
    const std::size_t n = static_cast<std::size_t>(duration_s * sample_rate);
    std::vector<float> out(n);
    constexpr float kTwoPi = 6.28318530717958647692f;
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = 0.5f * std::sin(kTwoPi * freq_hz *
                                 static_cast<float>(i) / static_cast<float>(sample_rate));
    }
    return out;
}

float median(std::vector<float> xs) {
    if (xs.empty()) return 0.0f;
    const auto mid = xs.begin() + static_cast<std::ptrdiff_t>(xs.size() / 2);
    std::nth_element(xs.begin(), mid, xs.end());
    return *mid;
}

std::vector<float> voiced_only(const PitchContour& c) {
    std::vector<float> out;
    for (float f : c.f0_hz) {
        if (f > 1.0f) out.push_back(f);
    }
    return out;
}

}  // namespace

int main() {
    PitchTrackerConfig cfg;
    cfg.sample_rate = 16000;
    PitchTracker tracker(cfg);

    // 200 Hz pure sine: median F0 should be within 2 Hz of truth.
    {
        const auto pcm = sine_wave(200.0f, 1.0f, cfg.sample_rate);
        const auto contour = tracker.track(pcm);
        if (contour.sample_rate != cfg.sample_rate)
            return fail("contour sample_rate should echo config");
        if (contour.rms.size() != contour.f0_hz.size())
            return fail("rms and f0_hz must have equal length");

        const auto voiced = voiced_only(contour);
        if (voiced.size() < 10) return fail("expected many voiced frames for pure sine");
        const float med = median(voiced);
        if (std::abs(med - 200.0f) > 2.0f) {
            std::cerr << "200 Hz sine → median " << med << std::endl;
            return fail("200 Hz tone median F0 off by more than 2 Hz");
        }
    }

    // 120 Hz pure sine.
    {
        const auto pcm = sine_wave(120.0f, 1.0f, cfg.sample_rate);
        const auto contour = tracker.track(pcm);
        const auto voiced = voiced_only(contour);
        const float med = median(voiced);
        if (std::abs(med - 120.0f) > 2.0f) {
            std::cerr << "120 Hz sine → median " << med << std::endl;
            return fail("120 Hz tone median F0 off by more than 2 Hz");
        }
    }

    // Silence should produce all-unvoiced frames.
    {
        const std::vector<float> silence(cfg.sample_rate, 0.0f);
        const auto contour = tracker.track(silence);
        for (float f : contour.f0_hz) {
            if (f > 1.0f) return fail("silence produced voiced frames");
        }
    }

    std::cout << "[test_pitch_tracker] OK" << std::endl;
    return 0;
}
