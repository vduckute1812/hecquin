#include "learning/prosody/Dtw.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using hecquin::learning::prosody::dtw_mean_abs_banded;

namespace {

int fail(const char* msg) {
    std::cerr << "[test_dtw_banded] FAIL: " << msg << std::endl;
    return 1;
}

bool approx_eq(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    // Identical sequences must align with zero cost.
    {
        const std::vector<float> a = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        const float cost = dtw_mean_abs_banded(a, a, /*band=*/2);
        if (!approx_eq(cost, 0.0f)) return fail("identical sequences cost zero");
    }

    // Shift-by-one same-length: the optimal warp matches each sample to
    // its neighbour with at most one off-diagonal step, so the cost is
    // strictly less than the constant-shift naive baseline.
    {
        const std::vector<float> a = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
        const std::vector<float> b = {1.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f}; // a shifted right by 1
        const float cost = dtw_mean_abs_banded(a, b, /*band=*/2);
        if (!(cost < 1.0f / static_cast<float>(a.size() + b.size()) * 6.0f)) {
            return fail("shift-by-one warp cheaper than worst-case");
        }
        if (!(cost > 0.0f)) return fail("shift-by-one cost positive");
    }

    // Completely different sequences: cost monotonic in magnitude.
    {
        const std::vector<float> a(8, 0.0f);
        const std::vector<float> b(8, 10.0f);
        const float cost = dtw_mean_abs_banded(a, b, /*band=*/2);
        // Every cell pays 10; mean over (N+M)=16 cells is 10·8 / 16 = 5.0.
        if (!approx_eq(cost, 5.0f, 1e-4f)) return fail("constant-offset cost = 5.0");
    }

    // Empty input must return +inf (caller treats as no alignment).
    {
        const std::vector<float> a;
        const std::vector<float> b = {1.0f};
        const float cost = dtw_mean_abs_banded(a, b, 1);
        if (!std::isinf(cost)) return fail("empty -> inf");
    }

    std::cout << "[test_dtw_banded] OK" << std::endl;
    return 0;
}
