#pragma once

#include "learning/pronunciation/PhonemeTypes.hpp"

#include <vector>

namespace hecquin::learning::pronunciation {

/**
 * Viterbi-based forced alignment of a phoneme target sequence over a CTC
 * emissions matrix (log-probabilities, `Emissions::blank_id` identifying the
 * blank token).
 *
 * Output: one `AlignSegment` per target phoneme, giving the frame span it
 * was aligned to and the mean log-posterior of the target id within that
 * span.  Repeated-phoneme separators are handled by requiring a blank
 * between identical targets as per the standard CTC rule.
 */
class CtcAligner {
public:
    /**
     * Align `targets` (phoneme ids, must all be >= 0 and != blank_id) against
     * `emissions`.  Returns `ok == false` when the sequence cannot be aligned
     * (e.g. emissions is empty or target is longer than frames allow).
     */
    [[nodiscard]] AlignResult align(const Emissions& emissions,
                                    const std::vector<int>& targets) const;
};

} // namespace hecquin::learning::pronunciation
