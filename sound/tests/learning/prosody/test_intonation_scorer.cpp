#include "learning/prosody/IntonationScorer.hpp"
#include "learning/prosody/PitchTracker.hpp"

#include <cmath>
#include <iostream>
#include <vector>

using namespace hecquin::learning::prosody;

namespace {

int fail(const char* msg) {
    std::cerr << "[test_intonation_scorer] FAIL: " << msg << std::endl;
    return 1;
}

PitchContour make_contour(const std::vector<float>& f0_hz,
                          float frame_hop_ms = 10.0f,
                          int sample_rate = 16000) {
    PitchContour c;
    c.sample_rate = sample_rate;
    c.frame_hop_ms = frame_hop_ms;
    c.f0_hz = f0_hz;
    c.rms.assign(f0_hz.size(), 0.5f);
    return c;
}

}  // namespace

int main() {
    IntonationScorer scorer;

    // Flat reference, 600 ms worth of frames at 10 ms/frame.
    const std::vector<float> flat(60, 180.0f);

    // Identical learner → high score, matching direction.
    {
        const auto ref = make_contour(flat);
        const auto usr = make_contour(flat);
        const auto s = scorer.score(ref, usr);
        if (s.overall_0_100 < 90.0f)
            return fail("identical contours should score > 90");
        if (s.reference_direction != FinalDirection::Flat)
            return fail("flat reference should be classified Flat");
        if (s.learner_direction != FinalDirection::Flat)
            return fail("flat learner should be classified Flat");
        if (!s.final_direction_match)
            return fail("matching directions should be flagged as a match");
    }

    // Rising reference vs falling learner → direction mismatch + score cap.
    {
        std::vector<float> rising(60);
        std::vector<float> falling(60);
        for (std::size_t i = 0; i < rising.size(); ++i) {
            rising[i] = 150.0f + static_cast<float>(i) * 1.5f;
            falling[i] = 240.0f - static_cast<float>(i) * 1.5f;
        }
        const auto ref = make_contour(rising);
        const auto usr = make_contour(falling);
        const auto s = scorer.score(ref, usr);
        if (s.reference_direction != FinalDirection::Rising)
            return fail("rising reference misclassified");
        if (s.learner_direction != FinalDirection::Falling)
            return fail("falling learner misclassified");
        if (s.final_direction_match)
            return fail("rising vs falling should NOT match");
        if (s.overall_0_100 > 60.0f)
            return fail("mismatched contour should be capped <= 60");
        if (s.issues.empty())
            return fail("mismatched direction should populate issues");
    }

    // Empty learner contour → safely return 0.
    {
        const auto ref = make_contour(flat);
        const auto usr = make_contour({});
        const auto s = scorer.score(ref, usr);
        if (s.overall_0_100 != 0.0f)
            return fail("empty learner contour should score 0");
    }

    // Direction helper — standalone sanity check.
    {
        std::vector<float> rising(60);
        for (std::size_t i = 0; i < rising.size(); ++i) {
            rising[i] = 150.0f + static_cast<float>(i) * 1.5f;
        }
        const auto c = make_contour(rising);
        if (scorer.final_direction(c) != FinalDirection::Rising)
            return fail("final_direction should detect rising");
    }

    std::cout << "[test_intonation_scorer] OK" << std::endl;
    return 0;
}
