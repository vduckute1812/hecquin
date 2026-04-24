#include "learning/prosody/IntonationScorer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace hecquin::learning::prosody {

namespace {

std::vector<float> voiced_semitones(const PitchContour& c) {
    std::vector<float> hz;
    hz.reserve(c.num_frames());
    for (float f : c.f0_hz) {
        if (f > 1.0f) hz.push_back(f);
    }
    if (hz.empty()) return {};
    std::vector<float> sorted = hz;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2),
                     sorted.end());
    const float median = sorted[sorted.size() / 2];
    if (median <= 0.0f) return {};
    std::vector<float> st;
    st.reserve(hz.size());
    for (float f : hz) {
        st.push_back(12.0f * std::log2(f / median));
    }
    return st;
}

// Banded DTW (Sakoe–Chiba) with rolling rows.  Memory is O(min(N,M)) rather
// than O(N·M) and runtime is O(max(N,M) · r) where `r` is the band radius.
// For the contours we see in practice (~300 frames at 10 ms hop), that's a
// ~10× improvement both in time and in space without changing the alignment
// path on similar-length inputs.
float dtw_mean_abs_banded(const std::vector<float>& a, const std::vector<float>& b, int band) {
    if (a.empty() || b.empty()) return std::numeric_limits<float>::infinity();
    const std::size_t N = a.size(), M = b.size();
    const std::size_t bw = band <= 0
        ? std::max(N, M)
        : static_cast<std::size_t>(band);

    constexpr double kInf = std::numeric_limits<double>::infinity();
    std::vector<double> prev(M + 1, kInf);
    std::vector<double> cur(M + 1, kInf);
    prev[0] = 0.0;

    for (std::size_t i = 1; i <= N; ++i) {
        // Center of the band on row i maps to j ≈ i * (M/N); reject cells
        // that fall outside [center - bw, center + bw].
        const double center = (N > 0)
            ? static_cast<double>(i) * static_cast<double>(M) /
              static_cast<double>(N)
            : 0.0;
        const std::size_t jlo = static_cast<std::size_t>(
            std::max<long long>(1, static_cast<long long>(std::ceil(center - static_cast<double>(bw)))));
        const std::size_t jhi = static_cast<std::size_t>(
            std::min<long long>(static_cast<long long>(M),
                                static_cast<long long>(std::floor(center + static_cast<double>(bw)))));

        std::fill(cur.begin(), cur.end(), kInf);
        if (jlo <= jhi) {
            for (std::size_t j = jlo; j <= jhi; ++j) {
                const double cost = std::abs(static_cast<double>(a[i - 1] - b[j - 1]));
                const double from_stay = prev[j];
                const double from_left = cur[j - 1];
                const double from_diag = prev[j - 1];
                double best = std::min({from_stay, from_left, from_diag});
                if (std::isinf(best)) continue;
                cur[j] = cost + best;
            }
        }
        std::swap(prev, cur);
    }
    if (std::isinf(prev[M])) {
        // Fall back to diagonal-only path cost so the caller never sees inf
        // (rare: only triggers when the band is too narrow for a degenerate
        // length mismatch).
        double diag_cost = 0.0;
        const std::size_t L = std::min(N, M);
        for (std::size_t k = 0; k < L; ++k) {
            diag_cost += std::abs(static_cast<double>(a[k] - b[k]));
        }
        return static_cast<float>(diag_cost / static_cast<double>(N + M));
    }
    // Path length for the diagonal-only baseline is max(N, M) ≤ len ≤ N + M.
    // Normalise by N + M to stay comparable across contour lengths.
    return static_cast<float>(prev[M] / static_cast<double>(N + M));
}

}  // namespace

IntonationScorer::IntonationScorer(IntonationScoreConfig cfg) : cfg_(cfg) {}

FinalDirection IntonationScorer::final_direction(const PitchContour& c) const {
    if (c.f0_hz.empty() || c.frame_hop_ms <= 0.0f) return FinalDirection::Unknown;
    const std::size_t window_frames = std::max<std::size_t>(2,
        static_cast<std::size_t>(cfg_.final_window_ms / c.frame_hop_ms));

    std::vector<std::pair<std::size_t, float>> tail;  // (frame index, f0)
    const std::size_t start = c.f0_hz.size() > window_frames
        ? c.f0_hz.size() - window_frames : 0;
    for (std::size_t i = start; i < c.f0_hz.size(); ++i) {
        if (c.f0_hz[i] > 1.0f) tail.emplace_back(i, c.f0_hz[i]);
    }
    if (tail.size() < 3) return FinalDirection::Unknown;

    // Average first-third vs last-third Hz for robustness against single-frame jitter.
    const std::size_t third = std::max<std::size_t>(1, tail.size() / 3);
    double first = 0.0, last = 0.0;
    for (std::size_t i = 0; i < third; ++i) first += tail[i].second;
    for (std::size_t i = tail.size() - third; i < tail.size(); ++i) last += tail[i].second;
    first /= static_cast<double>(third);
    last  /= static_cast<double>(third);

    // Compare in semitones rather than raw Hz so the threshold is symmetric
    // between low-pitched (male, ~100 Hz) and high-pitched (child, ~300 Hz)
    // voices — a 15 Hz rise at 100 Hz is a ~2.4 st jump but at 300 Hz it is
    // barely noticeable at ~0.8 st.
    if (first <= 0.0 || last <= 0.0) return FinalDirection::Unknown;
    const double st = 12.0 * std::log2(last / first);
    if (st >  cfg_.direction_delta_semitones) return FinalDirection::Rising;
    if (st < -cfg_.direction_delta_semitones) return FinalDirection::Falling;
    return FinalDirection::Flat;
}

IntonationScore IntonationScorer::score(const PitchContour& reference,
                                        const PitchContour& learner) const {
    IntonationScore out;
    const auto ref_st = voiced_semitones(reference);
    const auto lrn_st = voiced_semitones(learner);

    if (ref_st.empty() || lrn_st.empty()) {
        out.issues.emplace_back(ref_st.empty()
            ? "No pitch detected on the reference."
            : "I could not detect your pitch — speak a bit louder.");
        return out;
    }

    const std::size_t max_len = std::max(ref_st.size(), lrn_st.size());
    int band = 0;
    if (cfg_.band_ratio > 0.0f) {
        band = static_cast<int>(std::ceil(cfg_.band_ratio * static_cast<float>(max_len)));
        band = std::max(band, cfg_.band_min);
    }
    const float rmse = dtw_mean_abs_banded(ref_st, lrn_st, band);
    const float clamped = std::clamp(rmse, cfg_.best_semitone_rmse, cfg_.worst_semitone_rmse);
    const float t = (cfg_.worst_semitone_rmse - clamped) /
                    (cfg_.worst_semitone_rmse - cfg_.best_semitone_rmse);
    out.overall_0_100 = 100.0f * t;

    out.reference_direction = final_direction(reference);
    out.learner_direction   = final_direction(learner);
    out.final_direction_match = (out.reference_direction == out.learner_direction);

    if (!out.final_direction_match) {
        if (out.reference_direction == FinalDirection::Rising &&
            out.learner_direction   != FinalDirection::Rising) {
            out.issues.emplace_back("Your ending went flat or down — questions should rise.");
        } else if (out.reference_direction == FinalDirection::Falling &&
                   out.learner_direction   == FinalDirection::Rising) {
            out.issues.emplace_back("Your ending rose — statements should fall.");
        } else {
            out.issues.emplace_back("Your sentence ending did not match the target shape.");
        }
        out.overall_0_100 = std::min(out.overall_0_100, 60.0f);
    }
    return out;
}

const char* to_string(FinalDirection d) {
    switch (d) {
        case FinalDirection::Rising:  return "rising";
        case FinalDirection::Falling: return "falling";
        case FinalDirection::Flat:    return "flat";
        case FinalDirection::Unknown: return "unknown";
    }
    return "unknown";
}

} // namespace hecquin::learning::prosody
