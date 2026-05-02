#pragma once

namespace hecquin::voice {

struct CollectedUtterance;

/**
 * Verdict from the secondary VAD gate that runs after the end-silence
 * timer fires.  `accept` is true iff the utterance should be handed to
 * Whisper; otherwise the `too_*` flags carry the precise reason(s).
 * Multiple flags may be set if the audio missed on more than one knob.
 *
 * `crest_factor` is `peak_amplitude / mean_rms`.  Speech is "peaky"
 * (crest 8-15); wide-band noise sits at 3-5; constant tones near 1.4.
 * `too_flat` fires when the crest factor falls below the configured
 * floor and is the gate's defence against fans / A/C / hum that pass
 * the RMS check on energy alone.
 */
struct VadGateDecision {
    bool accept;
    bool too_quiet;
    bool too_sparse;
    bool too_flat = false;
    float mean_rms;
    float voiced_ratio;
    float crest_factor = 0.0f;
};

/**
 * Pure function form of the secondary gate.  Extracted so tests and
 * external tooling can drive it without needing the whole `VoiceListener`
 * infrastructure, and so the orchestration loop stays focused.
 *
 * `effective_frames` is total polled frames minus the tail silence that
 * triggered the end-of-utterance transition.  Must be ≥ 1.
 *
 * `crest_factor` is the peak-to-RMS ratio over the utterance; pass 0
 * (and `min_crest_factor = 0`) when the metric is unavailable.
 */
VadGateDecision evaluate_secondary_gate(int voiced_frames,
                                        int effective_frames,
                                        float mean_rms,
                                        float min_utterance_rms,
                                        float min_voiced_frame_ratio,
                                        float crest_factor = 0.0f,
                                        float min_crest_factor = 0.0f);

/**
 * Convenience overload that derives the gate inputs (`effective_frames`,
 * the utterance mean RMS, and the crest factor) directly from a
 * `CollectedUtterance`.
 *
 * The arithmetic used to live inline at the top of
 * `VoiceListener::run`'s loop body; moving it here keeps the
 * orchestrator focused on flow control and gives anyone replacing the
 * gate a single call site to redirect.
 *
 * Defined in [`SecondaryVadGate.cpp`](SecondaryVadGate.cpp); links the
 * collector header for the struct definition.
 */
VadGateDecision evaluate_for_utterance(const CollectedUtterance& utt,
                                       int poll_interval_ms,
                                       float min_utterance_rms,
                                       float min_voiced_frame_ratio,
                                       float min_crest_factor = 0.0f);

} // namespace hecquin::voice
