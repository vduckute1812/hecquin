#pragma once

#include "learning/pronunciation/PhonemeTypes.hpp"

#include <string>
#include <unordered_map>

namespace hecquin::learning::pronunciation {

struct PhonemeCalibration {
    float min_logp = -12.0f;
    float max_logp = 0.0f;
};

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

    /**
     * Per-phoneme calibration overrides, keyed by IPA.  Useful because
     * nasals and fricatives routinely post lower log-posteriors than
     * vowels even when articulated correctly — a global floor penalises
     * them unfairly.  Typical source: `.env/shared/models/pronunciation/calibration.json`.
     * Missing entries fall back to the global `(min_logp, max_logp)` pair.
     */
    std::unordered_map<std::string, PhonemeCalibration> per_phoneme;

    /** Load per-phoneme overrides from a JSON file; missing/malformed → no-op. */
    bool load_calibration_json(const std::string& path);
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
                                           const AlignResult& alignment) const;

    /** Convert a log-prob to 0..100, clamped to the config anchors. */
    [[nodiscard]] float logp_to_score(float logp) const;

    /**
     * Convert a log-prob to 0..100 using the per-phoneme override for
     * `ipa` when one is configured, otherwise the global anchors.
     */
    [[nodiscard]] float logp_to_score(float logp, const std::string& ipa) const;

private:
    PronunciationScorerConfig cfg_;
};

} // namespace hecquin::learning::pronunciation
