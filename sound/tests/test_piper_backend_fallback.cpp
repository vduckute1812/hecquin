// PiperFallbackBackend strategy tests.  Uses stub IPiperBackend implementations
// so the fallback chain can be driven deterministically without spawning Piper.

#include "tts/backend/IPiperBackend.hpp"
#include "tts/backend/PiperFallbackBackend.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using hecquin::tts::backend::IPiperBackend;
using hecquin::tts::backend::PiperFallbackBackend;

namespace {

int failures = 0;

void expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "[FAIL] " << label << std::endl;
        ++failures;
    }
}

// Canned backend: reports success with a known tag so the test can
// identify which strategy produced the output, and counts its invocations.
struct StubBackend final : public IPiperBackend {
    explicit StubBackend(bool succeed_, std::int16_t tag_, int rate_ = 22050)
        : succeed(succeed_), tag(tag_), rate(rate_) {}

    bool synthesize(const std::string& /*text*/, const std::string& /*model*/,
                    std::vector<std::int16_t>& samples_out,
                    int& sample_rate_out) override {
        ++calls;
        if (!succeed) return false;                  // must leave outputs untouched on failure
        samples_out = {tag, tag, tag};
        sample_rate_out = rate;
        return true;
    }

    bool succeed;
    std::int16_t tag;
    int rate;
    int calls = 0;
};

} // namespace

int main() {
    // 1. Primary succeeds → fallback is never invoked.
    {
        auto primary  = std::make_unique<StubBackend>(true,  std::int16_t{11});
        auto fallback = std::make_unique<StubBackend>(true,  std::int16_t{22});
        auto* primary_raw  = primary.get();
        auto* fallback_raw = fallback.get();
        PiperFallbackBackend be{std::move(primary), std::move(fallback)};

        std::vector<std::int16_t> out;
        int rate = 0;
        bool ok = be.synthesize("hello", "/models/voice.onnx", out, rate);
        expect(ok, "primary-success path returns true");
        expect(primary_raw->calls == 1,  "primary invoked exactly once");
        expect(fallback_raw->calls == 0, "fallback skipped when primary succeeds");
        expect(!out.empty() && out[0] == 11, "primary samples propagated");
    }

    // 2. Primary fails → fallback runs and its samples are returned.
    {
        auto primary  = std::make_unique<StubBackend>(false, std::int16_t{11});
        auto fallback = std::make_unique<StubBackend>(true,  std::int16_t{22});
        auto* primary_raw  = primary.get();
        auto* fallback_raw = fallback.get();
        PiperFallbackBackend be{std::move(primary), std::move(fallback)};

        std::vector<std::int16_t> out;
        int rate = 0;
        bool ok = be.synthesize("hello", "/models/voice.onnx", out, rate);
        expect(ok, "fallback-success path returns true");
        expect(primary_raw->calls == 1,  "primary still attempted first");
        expect(fallback_raw->calls == 1, "fallback invoked after primary failure");
        expect(!out.empty() && out[0] == 22, "fallback samples are what we see");
    }

    // 3. Both fail → overall synthesis reports failure and clears outputs.
    {
        auto primary  = std::make_unique<StubBackend>(false, std::int16_t{11});
        auto fallback = std::make_unique<StubBackend>(false, std::int16_t{22});
        auto* primary_raw  = primary.get();
        auto* fallback_raw = fallback.get();
        PiperFallbackBackend be{std::move(primary), std::move(fallback)};

        std::vector<std::int16_t> out = {1, 2, 3};
        int rate = 16000;
        bool ok = be.synthesize("hello", "/models/voice.onnx", out, rate);
        expect(!ok, "both-fail path returns false");
        expect(out.empty(), "samples cleared on total failure");
        expect(rate == 0,   "sample rate cleared on total failure");
        expect(primary_raw->calls == 1 && fallback_raw->calls == 1,
               "both strategies attempted exactly once");
    }

    // 4. Null primary must not crash — fallback alone is still honoured.
    {
        auto fallback = std::make_unique<StubBackend>(true, std::int16_t{22});
        auto* fallback_raw = fallback.get();
        PiperFallbackBackend be{nullptr, std::move(fallback)};

        std::vector<std::int16_t> out;
        int rate = 0;
        bool ok = be.synthesize("hello", "/models/voice.onnx", out, rate);
        expect(ok, "fallback alone still succeeds when primary is null");
        expect(fallback_raw->calls == 1, "fallback invoked once");
    }

    if (failures == 0) {
        std::cout << "[test_piper_backend_fallback] all assertions passed" << std::endl;
        return 0;
    }
    return 1;
}
