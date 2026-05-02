#include "voice/PipelineTelemetry.hpp"

#include <sstream>
#include <string>
#include <utility>

namespace hecquin::voice {

namespace {

// Compose a stable string key listing every gate flag that fired,
// joined with "+".  Single-flag rejections keep their original tags
// ("too_quiet", "too_sparse") so existing dashboards stay byte-identical;
// only multi-flag combinations have a "+too_flat" suffix.
std::string rejection_reason(const VadGateDecision& g) {
    std::string out;
    auto append = [&out](const char* tag) {
        if (!out.empty()) out += "+";
        out += tag;
    };
    if (g.too_quiet)  append("too_quiet");
    if (g.too_sparse) append("too_sparse");
    if (g.too_flat)   append("too_flat");
    if (out.empty()) out = "unknown";
    return out;
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
          << ",\"crest_factor\":" << gate.crest_factor
          << ",\"speech_ms\":" << speech_ms << "}";
    sink_({"vad_gate", "skipped", speech_ms, attrs.str()});
}

void PipelineTelemetry::emit_vad_continue_clamped(float raw_continue_thr,
                                                    float applied_continue_thr,
                                                    float start_thr) {
    if (!sink_) return;
    std::ostringstream attrs;
    attrs << "{\"raw_continue_thr\":" << raw_continue_thr
          << ",\"applied_continue_thr\":" << applied_continue_thr
          << ",\"start_thr\":" << start_thr << "}";
    sink_({"vad_continue_clamped", "applied", 0, attrs.str()});
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
