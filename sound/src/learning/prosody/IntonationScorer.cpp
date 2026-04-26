#include "learning/prosody/IntonationScorer.hpp"

#include "learning/prosody/Dtw.hpp"

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
