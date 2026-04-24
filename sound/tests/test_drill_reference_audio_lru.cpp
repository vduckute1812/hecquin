// DrillReferenceAudio LRU cache tests.  Uses the `put_for_test` /
// `lookup_for_test` hooks to exercise the eviction + MRU-bump policy with
// zero Piper / SDL involvement.

#include "learning/prosody/PitchTracker.hpp"
#include "learning/pronunciation/drill/DrillReferenceAudio.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using hecquin::learning::pronunciation::drill::DrillReferenceAudio;
using hecquin::learning::prosody::PitchContour;

namespace {

int failures = 0;

void expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "[FAIL] " << label << std::endl;
        ++failures;
    }
}

PitchContour make_contour(float f0) {
    PitchContour c;
    c.f0_hz = {f0, f0 + 1.0f, f0 + 2.0f};
    c.rms   = {0.1f, 0.1f, 0.1f};
    return c;
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // 1. Basic insert + lookup.
    // ------------------------------------------------------------------
    {
        DrillReferenceAudio ref({/*piper=*/"", /*cache_size=*/3});
        ref.put_for_test("a", make_contour(100.0f), {}, 22050);
        ref.put_for_test("b", make_contour(200.0f), {}, 22050);
        expect(ref.cache_size_for_test() == 2, "two entries inserted");
        const auto* got = ref.lookup_for_test("a");
        expect(got != nullptr, "lookup returns inserted key");
        expect(got && !got->f0_hz.empty() && got->f0_hz[0] == 100.0f,
               "lookup returns correct contour");
        expect(ref.lookup_for_test("missing") == nullptr,
               "lookup of missing key returns null");
    }

    // ------------------------------------------------------------------
    // 2. Eviction once capacity is exceeded (LRU victim).
    // ------------------------------------------------------------------
    {
        DrillReferenceAudio ref({/*piper=*/"", /*cache_size=*/2});
        ref.put_for_test("a", make_contour(100.0f), {}, 22050);
        ref.put_for_test("b", make_contour(200.0f), {}, 22050);
        ref.put_for_test("c", make_contour(300.0f), {}, 22050);

        expect(ref.cache_size_for_test() == 2, "cache capped at 2 after 3 inserts");
        expect(ref.lookup_for_test("a") == nullptr,
               "oldest entry 'a' is evicted");
        expect(ref.lookup_for_test("b") != nullptr, "entry 'b' survives");
        expect(ref.lookup_for_test("c") != nullptr, "entry 'c' survives");
    }

    // ------------------------------------------------------------------
    // 3. MRU bump: a `lookup` on 'a' saves it from being evicted next.
    // ------------------------------------------------------------------
    {
        DrillReferenceAudio ref({/*piper=*/"", /*cache_size=*/2});
        ref.put_for_test("a", make_contour(100.0f), {}, 22050);
        ref.put_for_test("b", make_contour(200.0f), {}, 22050);
        // Touch 'a' so 'b' becomes the LRU victim.
        (void)ref.lookup_for_test("a");
        ref.put_for_test("c", make_contour(300.0f), {}, 22050);

        expect(ref.lookup_for_test("a") != nullptr,
               "'a' survives because of MRU bump");
        expect(ref.lookup_for_test("b") == nullptr,
               "'b' is evicted as new LRU victim");
        expect(ref.lookup_for_test("c") != nullptr, "newly inserted 'c' present");
    }

    // ------------------------------------------------------------------
    // 4. cache_size == 0 disables the cache entirely.
    // ------------------------------------------------------------------
    {
        DrillReferenceAudio ref({/*piper=*/"", /*cache_size=*/0});
        ref.put_for_test("a", make_contour(100.0f), {}, 22050);
        expect(ref.cache_size_for_test() == 0,
               "disabled cache ignores put_for_test");
        expect(ref.lookup_for_test("a") == nullptr,
               "disabled cache never reports a hit");
    }

    // ------------------------------------------------------------------
    // 5. MRU ordering is observable via cache_keys_for_test.
    // ------------------------------------------------------------------
    {
        DrillReferenceAudio ref({/*piper=*/"", /*cache_size=*/3});
        ref.put_for_test("a", make_contour(100.0f), {}, 22050);
        ref.put_for_test("b", make_contour(200.0f), {}, 22050);
        ref.put_for_test("c", make_contour(300.0f), {}, 22050);

        auto keys = ref.cache_keys_for_test();
        expect(keys.size() == 3, "three keys in MRU list");
        expect(keys.front() == "c", "most-recently-inserted at front");
        expect(keys.back()  == "a", "oldest at back");

        (void)ref.lookup_for_test("a");
        keys = ref.cache_keys_for_test();
        expect(keys.front() == "a", "lookup bumps key to front");
        expect(keys.back()  == "b", "previously-middle key becomes new LRU");
    }

    if (failures == 0) {
        std::cout << "[test_drill_reference_audio_lru] all assertions passed" << std::endl;
        return 0;
    }
    return 1;
}
