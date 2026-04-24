#pragma once

#include "learning/prosody/PitchTracker.hpp"

#include <string>
#include <vector>

namespace hecquin::learning::prosody {

enum class FinalDirection {
    Unknown,
    Rising,      ///< yes/no question-like
    Falling,     ///< statement
    Flat,
};

struct IntonationScoreConfig {
    /** Length of the "final-phrase" window in ms for direction analysis. */
    float final_window_ms = 500.0f;
    /**
     * Minimum absolute pitch change across the final window that counts as a
     * rise / fall.  Expressed in semitones so the threshold is perceptually
     * symmetric between low-pitched and high-pitched voices (1 semitone ≈ 6 %
     * of the base frequency).  Treat `≈ 1.5` as the default; anything smaller
     * gets classified as `Flat`.
     */
    float direction_delta_semitones = 1.5f;
    /** Floor / ceiling for the 0..100 mapping of DTW distance (in semitone RMSE). */
    float worst_semitone_rmse = 6.0f;
    float best_semitone_rmse = 0.5f;
    /**
     * Sakoe–Chiba band radius as a fraction of max(N, M).  The optimal
     * alignment between similar contours rarely drifts more than ~10 % of
     * the sequence length off the diagonal, so restricting search to that
     * band cuts DTW's runtime and memory from O(N·M) to O(N·r) without
     * quality loss on prosody.  Values ≤ 0 fall back to unbanded DTW.
     */
    float band_ratio = 0.1f;
    /** Absolute lower bound for the band radius, so very short contours still allow some warping. */
    int band_min = 8;
};

struct IntonationScore {
    float overall_0_100 = 0.0f;
    FinalDirection reference_direction = FinalDirection::Unknown;
    FinalDirection learner_direction   = FinalDirection::Unknown;
    bool final_direction_match = false;
    /** Short human-readable flags for TTS feedback. */
    std::vector<std::string> issues;
};

/**
 * Score learner-vs-reference prosody.
 *
 * Step 1: drop unvoiced frames, convert Hz → semitones relative to each
 *         contour's own median (so absolute pitch differences between
 *         voices wash out and we only compare melodic shape).
 * Step 2: dynamic-time-warping distance between the two semitone series.
 *         RMSE of the aligned path maps linearly onto a 0..100 score
 *         between `worst_semitone_rmse` and `best_semitone_rmse`.
 * Step 3: final-window direction rule — if the reference ends rising and
 *         the learner ends falling (or vice versa), flag it and cap the
 *         overall at 60 so the rule bites visibly.
 */
class IntonationScorer {
public:
    explicit IntonationScorer(IntonationScoreConfig cfg = {});

    [[nodiscard]] IntonationScore score(const PitchContour& reference,
                                        const PitchContour& learner) const;

    /** Inspectable helper — direction of the last `final_window_ms` of a contour. */
    [[nodiscard]] FinalDirection final_direction(const PitchContour& c) const;

private:
    IntonationScoreConfig cfg_;
};

[[nodiscard]] const char* to_string(FinalDirection d);

} // namespace hecquin::learning::prosody
