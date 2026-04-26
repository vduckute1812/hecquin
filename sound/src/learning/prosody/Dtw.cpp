#include "learning/prosody/Dtw.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hecquin::learning::prosody {

float dtw_mean_abs_banded(const std::vector<float>& a,
                          const std::vector<float>& b,
                          int band) {
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

} // namespace hecquin::learning::prosody
