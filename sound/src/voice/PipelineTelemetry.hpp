#pragma once

#include "voice/SecondaryVadGate.hpp"
#include "voice/VoiceListener.hpp"

#include <cstddef>

namespace hecquin::voice {

/**
 * Owns the `PipelineEventSink` and the JSON-attribute formatting for
 * every per-stage telemetry event the listener emits.
 *
 * Before this collaborator existed, `VoiceListener::run` and
 * `handle_vad_rejection_` each contained their own `std::ostringstream`
 * blocks producing hand-rolled JSON.  Centralising the formatting here
 * keeps the listener focused on flow control, makes it harder to drift
 * the schema of one event vs another, and creates an obvious place to
 * add a real JSON library or a structured-logging backend later.
 *
 * Construction is cheap; copies and moves are allowed because the only
 * state is a `std::function` sink.  Calling any `emit_*` method on a
 * telemetry built from a null sink is a no-op.
 */
class PipelineTelemetry {
public:
    PipelineTelemetry() = default;
    explicit PipelineTelemetry(PipelineEventSink sink);

    /** Replace the sink (used by `VoiceListener::setPipelineEventSink`). */
    void set_sink(PipelineEventSink sink);

    /** True iff a non-null sink has been installed. */
    bool enabled() const { return static_cast<bool>(sink_); }

    /** Secondary VAD gate rejected an utterance. */
    void emit_vad_rejection(const VadGateDecision& gate, int speech_ms);

    /** Whisper finished (or was filtered to empty). */
    void emit_whisper(long latency_ms, float no_speech_prob,
                      std::size_t chars, int speech_ms, bool ok);

private:
    PipelineEventSink sink_;
};

} // namespace hecquin::voice
