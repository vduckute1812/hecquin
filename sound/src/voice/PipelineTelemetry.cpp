#include "voice/PipelineTelemetry.hpp"

#include <sstream>
#include <utility>

namespace hecquin::voice {

namespace {

const char* rejection_reason(const VadGateDecision& g) {
    if (g.too_quiet && g.too_sparse) return "too_quiet+too_sparse";
    if (g.too_quiet)                  return "too_quiet";
    return "too_sparse";
}

} // namespace

PipelineTelemetry::PipelineTelemetry(PipelineEventSink sink)
    : sink_(std::move(sink)) {}

void PipelineTelemetry::set_sink(PipelineEventSink sink) {
    sink_ = std::move(sink);
}

void PipelineTelemetry::emit_vad_rejection(const VadGateDecision& gate,
                                           int speech_ms) {
    if (!sink_) return;
    // Tiny hand-rolled JSON — avoids pulling nlohmann into the voice
    // library just for a couple of attrs.  Keeping the formatting here
    // (rather than inline at the call site) means every new caller gets
    // the same field schema by construction.
    std::ostringstream attrs;
    attrs << "{\"reason\":\"" << rejection_reason(gate)
          << "\",\"mean_rms\":" << gate.mean_rms
          << ",\"voiced_ratio\":" << gate.voiced_ratio
          << ",\"speech_ms\":" << speech_ms << "}";
    sink_({"vad_gate", "skipped", speech_ms, attrs.str()});
}

void PipelineTelemetry::emit_whisper(long latency_ms,
                                     float no_speech_prob,
                                     std::size_t chars,
                                     int speech_ms,
                                     bool ok) {
    if (!sink_) return;
    std::ostringstream attrs;
    attrs << "{\"no_speech_prob\":" << no_speech_prob
          << ",\"chars\":" << chars
          << ",\"speech_ms\":" << speech_ms << "}";
    sink_({"whisper", ok ? "ok" : "skipped", latency_ms, attrs.str()});
}

} // namespace hecquin::voice
