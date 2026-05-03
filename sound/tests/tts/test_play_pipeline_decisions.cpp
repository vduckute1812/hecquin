// Pure planner helpers from PlayPipeline.hpp — streaming vs buffered fallback
// and abort outcome resolution — exercised without SDL or Piper.

#include "tts/PlayPipeline.hpp"

#include <iostream>

namespace {

using hecquin::tts::detail::StreamingDecision;
using hecquin::tts::detail::decide_streaming_path;
using hecquin::tts::detail::resolve_streaming_outcome;

int failures = 0;

void expect(bool ok, const char* label) {
    if (!ok) {
        std::cerr << "[FAIL] " << label << std::endl;
        ++failures;
    }
}

} // namespace

int main() {
    using SD = StreamingDecision;

    static_assert(decide_streaming_path(false, false) == SD::FallbackToBuffered,
                  "SDL fail → fallback");
    static_assert(decide_streaming_path(false, true) == SD::FallbackToBuffered,
                  "SDL fail even if spawn ok → fallback");
    static_assert(decide_streaming_path(true, false) == SD::FallbackToBuffered,
                  "spawn fail → fallback");
    static_assert(decide_streaming_path(true, true) == SD::Stream,
                  "both ok → stream");

    expect(resolve_streaming_outcome(/*aborted=*/true, /*exit_ok=*/true) == false,
           "abort ⇒ false regardless of exit status");
    expect(resolve_streaming_outcome(/*aborted=*/false, /*exit_ok=*/false) == false,
           "clean finish + bad exit ⇒ false");
    expect(resolve_streaming_outcome(/*aborted=*/false, /*exit_ok=*/true) == true,
           "clean finish + good exit ⇒ true");

    if (failures == 0) {
        std::cout << "[test_play_pipeline_decisions] OK" << std::endl;
        return 0;
    }
    std::cerr << "[test_play_pipeline_decisions] " << failures << " failure(s)"
              << std::endl;
    return 1;
}
