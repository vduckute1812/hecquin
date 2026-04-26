#pragma once

#include <vector>

namespace hecquin::learning::prosody {

/**
 * Banded Dynamic Time Warping with mean-abs cost (Sakoe–Chiba band,
 * rolling rows).  Aligns two 1-D sequences `a` and `b` and returns the
 * accumulated absolute-difference cost along the optimal warping path,
 * normalised by `(N + M)` so the value is comparable across contour
 * lengths.
 *
 *   - Cells outside `[i·(M/N) ± band]` are not visited; a `band` of
 *     0 (or negative) disables the band entirely (full DTW).
 *   - Memory is `O(min(N, M))` thanks to the two-row rolling buffer.
 *   - Runtime is `O(max(N, M) · band)` once a band is enabled.
 *
 * If the band is too narrow for a degenerate length mismatch the
 * function transparently falls back to a diagonal-only baseline so the
 * caller never sees `+inf`.
 *
 * Lives in its own translation unit because the routine is purely
 * numeric — it has no scorer / contour state — and is independently
 * testable (`tests/learning/prosody/test_dtw_banded.cpp`).
 */
float dtw_mean_abs_banded(const std::vector<float>& a,
                          const std::vector<float>& b,
                          int band);

} // namespace hecquin::learning::prosody
