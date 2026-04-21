#pragma once

#include "learning/pronunciation/PhonemeTypes.hpp"

#include <string>

namespace hecquin::learning::pronunciation {

struct PronunciationScorerConfig {
    /**
     * Floor for individual phoneme log-posteriors before mapping to 0..100.
     * -12 logp ≈ 6e-6 posterior — anything worse is treated as "completely
     * wrong" and mapped to 0.
     */
    float min_logp = -12.0f;
    /**
     * Upper anchor for the 0..100 mapping.  0 logp = 1.0 posterior = 100.
     */
    float max_logp = 0.0f;
};

/**
 * Turns emissions + alignment segments + target word structure into a
 * human-digestible `PronunciationScore`.  The per-phoneme number is the
 * mean log-posterior of the target id across its aligned frames, mapped
 * linearly onto 0..100 between `min_logp` and `max_logp`.
 */
class PronunciationScorer {
public:
    explicit PronunciationScorer(PronunciationScorerConfig cfg = {});

    /**
     * Score an alignment against a per-word phoneme plan.  The number of
     * alignment segments must match the flat target count.  Returns an
     * empty score on mismatch.
     */
    [[nodiscard]] PronunciationScore score(const G2PResult& plan,
                                           const AlignResult& alignment,
                                           float frame_stride_ms) const;

    /** Convert a log-prob to 0..100, clamped to the config anchors. */
    [[nodiscard]] float logp_to_score(float logp) const;

private:
    PronunciationScorerConfig cfg_;
};

} // namespace hecquin::learning::pronunciation
