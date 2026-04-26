#include "learning/pronunciation/PhonemeTypes.hpp"
#include "learning/pronunciation/PronunciationScorer.hpp"

#include <iostream>

using namespace hecquin::learning::pronunciation;

namespace {

int fail(const char* msg) {
    std::cerr << "[test_pronunciation_scorer] FAIL: " << msg << std::endl;
    return 1;
}

}  // namespace

int main() {
    // Plan: two words — "hi" (phonemes h, aɪ) and "bye" (phonemes b, aɪ).
    G2PResult plan;
    {
        WordPhonemes w1; w1.word = "hi";
        w1.phonemes = {{10, "h"}, {11, "aɪ"}};
        WordPhonemes w2; w2.word = "bye";
        w2.phonemes = {{12, "b"}, {11, "aɪ"}};
        plan.words = {w1, w2};
    }

    AlignResult align;
    align.ok = true;
    align.segments.push_back({10, 0, 2, -0.1f});     // h — good
    align.segments.push_back({11, 2, 5, -0.3f});     // aɪ — good
    align.segments.push_back({12, 5, 7, -0.2f});     // b  — good
    align.segments.push_back({11, 7, 10, -9.5f});    // aɪ — mispronounced (low logp)

    PronunciationScorer scorer;
    const auto score = scorer.score(plan, align);

    if (score.words.size() != 2) return fail("two word scores expected");

    const auto hi = score.words[0].score_0_100;
    const auto bye = score.words[1].score_0_100;
    if (hi <= bye) return fail("'hi' should score higher than mispronounced 'bye'");

    // Overall should sit between the two word scores.
    if (score.overall_0_100 < bye || score.overall_0_100 > hi)
        return fail("overall should be bounded by the two word scores");

    // Scores must be in [0, 100].
    for (const auto& w : score.words) {
        if (w.score_0_100 < 0.0f || w.score_0_100 > 100.0f)
            return fail("word score out of [0, 100]");
        for (const auto& p : w.phonemes) {
            if (p.score_0_100 < 0.0f || p.score_0_100 > 100.0f)
                return fail("phoneme score out of [0, 100]");
        }
    }

    // A log-posterior near 0 should map to ~100.
    if (scorer.logp_to_score(-0.01f) < 99.0f)
        return fail("logp ≈ 0 should map to near 100");
    // A log-posterior at the floor should map to 0.
    if (scorer.logp_to_score(-20.0f) > 0.01f)
        return fail("logp below floor should clamp to 0");

    // Per-phoneme calibration: apply a lenient window for "θ" only.  The
    // same logp that maps to 0 under the default (-12, 0) anchors should
    // map higher when "θ" is given a tighter floor.
    {
        PronunciationScorerConfig cfg;
        cfg.per_phoneme["θ"] = PhonemeCalibration{-4.0f, 0.0f};
        PronunciationScorer calibrated(cfg);
        const float logp = -2.0f;
        const float generic = calibrated.logp_to_score(logp);            // (-12, 0) → ~83
        const float override_ = calibrated.logp_to_score(logp, "θ");     // (-4, 0) → 50
        const float other     = calibrated.logp_to_score(logp, "s");     // no override → same as generic
        if (override_ >= generic) return fail("tighter override should score lower");
        if (other < generic - 0.01f || other > generic + 0.01f)
            return fail("missing override should fall through to global");
    }

    std::cout << "[test_pronunciation_scorer] OK" << std::endl;
    return 0;
}
