#include "learning/pronunciation/CtcAligner.hpp"

#include <cmath>
#include <iostream>

using namespace hecquin::learning::pronunciation;

namespace {

int fail(const char* msg) {
    std::cerr << "[test_ctc_aligner] FAIL: " << msg << std::endl;
    return 1;
}

// Build a fake emissions matrix with `num_frames` rows, `vocab_size` columns.
Emissions make_emissions(std::size_t num_frames, std::size_t vocab_size) {
    Emissions e;
    e.blank_id = 0;
    e.logits.assign(num_frames, std::vector<float>(vocab_size, -10.0f));
    return e;
}

}  // namespace

int main() {
    // Target: phonemes {2, 3, 2} over a vocab of 4 tokens {blank=0, 1, 2, 3}.
    // Construct a trellis where the best path is B B 2 2 3 3 2 B (8 frames).
    Emissions e = make_emissions(8, 4);

    auto set_peak = [&](std::size_t t, int tok) {
        for (auto& v : e.logits[t]) v = -10.0f;
        e.logits[t][static_cast<std::size_t>(tok)] = -0.05f;
    };

    set_peak(0, 0);   // blank
    set_peak(1, 0);   // blank
    set_peak(2, 2);   // phoneme 2
    set_peak(3, 2);
    set_peak(4, 3);   // phoneme 3
    set_peak(5, 3);
    set_peak(6, 2);   // phoneme 2 again (needs intervening blank in CTC)
    set_peak(7, 0);   // trailing blank

    CtcAligner aligner;
    const std::vector<int> targets{2, 3, 2};
    const auto result = aligner.align(e, targets);

    if (!result.ok) return fail("alignment should succeed");
    if (result.segments.size() != targets.size())
        return fail("one segment per target");

    // Segment 0: should cover frames 2..4 (phoneme 2).
    const auto& s0 = result.segments[0];
    if (s0.phoneme_id != 2) return fail("segment 0 not phoneme 2");
    if (s0.start_frame != 2 || s0.end_frame != 4)
        return fail("segment 0 frame span not 2..4");

    // Segment 1: frames 4..6 (phoneme 3).
    const auto& s1 = result.segments[1];
    if (s1.phoneme_id != 3) return fail("segment 1 not phoneme 3");
    if (s1.start_frame != 4 || s1.end_frame != 6)
        return fail("segment 1 frame span not 4..6");

    // Segment 2: frames 6..7 (second phoneme 2).
    const auto& s2 = result.segments[2];
    if (s2.phoneme_id != 2) return fail("segment 2 not phoneme 2");
    if (s2.start_frame != 6 || s2.end_frame < 7)
        return fail("segment 2 frame span not ~6..7");

    // Log-posterior should be close to -0.05 (the peak value) for all segments.
    for (const auto& seg : result.segments) {
        if (seg.log_posterior < -1.0f)
            return fail("log_posterior unexpectedly low");
    }

    // Reject when target has more phonemes than frames support.
    {
        Emissions small = make_emissions(1, 4);
        const auto r = aligner.align(small, {2, 3, 2});
        if (r.ok) return fail("should fail when frames < required");
    }

    std::cout << "[test_ctc_aligner] OK" << std::endl;
    return 0;
}
