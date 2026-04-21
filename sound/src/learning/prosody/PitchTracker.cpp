#include "learning/prosody/PitchTracker.hpp"

#include <algorithm>
#include <cmath>

namespace hecquin::learning::prosody {

namespace {

// One YIN frame → (f0_hz, rms).  Implements steps 1–5 of the YIN paper
// (difference function, cumulative mean normalised difference, absolute
// threshold, parabolic interpolation).  Step 6 (best local estimate within
// window) is omitted — good enough for sentence-level intonation scoring.
std::pair<float, float> yin_frame(const float* x,
                                  std::size_t W,
                                  int sample_rate,
                                  float min_f0_hz,
                                  float max_f0_hz,
                                  float threshold) {
    // RMS for voicing gate downstream.
    double rms_sq = 0.0;
    for (std::size_t i = 0; i < W; ++i) rms_sq += static_cast<double>(x[i]) * x[i];
    const float rms = static_cast<float>(std::sqrt(rms_sq / static_cast<double>(W)));

    if (W < 4 || rms < 1e-5f) {
        return {0.0f, rms};
    }

    const std::size_t tau_min = static_cast<std::size_t>(
        std::max(2.0f, std::floor(static_cast<float>(sample_rate) / max_f0_hz)));
    const std::size_t tau_max = static_cast<std::size_t>(
        std::min(static_cast<double>(W / 2),
                 std::ceil(static_cast<double>(sample_rate) / min_f0_hz)));
    if (tau_max <= tau_min + 1) return {0.0f, rms};

    std::vector<float> d(tau_max + 1, 0.0f);
    // Step 1: difference function d[tau] = sum_j (x[j] - x[j+tau])^2.
    for (std::size_t tau = 1; tau <= tau_max; ++tau) {
        double sum = 0.0;
        const std::size_t n = W - tau;
        for (std::size_t j = 0; j < n; ++j) {
            const float diff = x[j] - x[j + tau];
            sum += static_cast<double>(diff) * diff;
        }
        d[tau] = static_cast<float>(sum);
    }

    // Step 2: cumulative mean normalised difference (CMND).
    std::vector<float> cmnd(tau_max + 1, 1.0f);
    double running = 0.0;
    for (std::size_t tau = 1; tau <= tau_max; ++tau) {
        running += static_cast<double>(d[tau]);
        if (running <= 0.0) { cmnd[tau] = 1.0f; continue; }
        cmnd[tau] = static_cast<float>(d[tau] * static_cast<double>(tau) / running);
    }

    // Step 3: absolute threshold — first dip under `threshold`, continuing
    // while the curve keeps descending.  Falls back to the global minimum.
    std::size_t tau_est = 0;
    for (std::size_t tau = tau_min; tau <= tau_max; ++tau) {
        if (cmnd[tau] < threshold) {
            while (tau + 1 <= tau_max && cmnd[tau + 1] < cmnd[tau]) ++tau;
            tau_est = tau;
            break;
        }
    }
    if (tau_est == 0) {
        float best = cmnd[tau_min];
        tau_est = tau_min;
        for (std::size_t tau = tau_min + 1; tau <= tau_max; ++tau) {
            if (cmnd[tau] < best) { best = cmnd[tau]; tau_est = tau; }
        }
        if (best > 2.0f * threshold) return {0.0f, rms};
    }

    // Step 4: parabolic interpolation around tau_est for sub-sample accuracy.
    double tau_refined = static_cast<double>(tau_est);
    if (tau_est > tau_min && tau_est + 1 <= tau_max) {
        const double s0 = cmnd[tau_est - 1];
        const double s1 = cmnd[tau_est];
        const double s2 = cmnd[tau_est + 1];
        const double denom = 2.0 * (2.0 * s1 - s0 - s2);
        if (std::abs(denom) > 1e-9) {
            tau_refined += (s0 - s2) / denom;
        }
    }

    if (tau_refined <= 0.0) return {0.0f, rms};
    const float f0 = static_cast<float>(static_cast<double>(sample_rate) / tau_refined);
    if (f0 < min_f0_hz || f0 > max_f0_hz) return {0.0f, rms};
    return {f0, rms};
}

}  // namespace

PitchTracker::PitchTracker(PitchTrackerConfig cfg) : cfg_(cfg) {}

PitchContour PitchTracker::track(const std::vector<float>& pcm) const {
    PitchContour out;
    out.sample_rate = cfg_.sample_rate;
    out.frame_hop_ms = static_cast<float>(cfg_.frame_hop_samples) * 1000.0f /
                       static_cast<float>(cfg_.sample_rate);

    const std::size_t W = static_cast<std::size_t>(cfg_.frame_size_samples);
    const std::size_t H = static_cast<std::size_t>(cfg_.frame_hop_samples);
    if (pcm.size() < W) return out;

    const std::size_t n_frames = (pcm.size() - W) / H + 1;
    out.f0_hz.reserve(n_frames);
    out.rms.reserve(n_frames);
    for (std::size_t start = 0; start + W <= pcm.size(); start += H) {
        auto [f0, rms] = yin_frame(pcm.data() + start, W,
                                   cfg_.sample_rate,
                                   cfg_.min_f0_hz,
                                   cfg_.max_f0_hz,
                                   cfg_.voicing_threshold);
        out.f0_hz.push_back(f0);
        out.rms.push_back(rms);
    }
    return out;
}

} // namespace hecquin::learning::prosody
