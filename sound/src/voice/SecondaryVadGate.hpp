#pragma once

namespace hecquin::voice {

/**
 * Verdict from the secondary VAD gate that runs after the end-silence
 * timer fires.  `accept` is true iff the utterance should be handed to
 * Whisper; otherwise `too_quiet` and/or `too_sparse` flags carry the
 * precise reason(s).  Both may be set if the audio missed on both knobs.
 */
struct VadGateDecision {
    bool accept;
    bool too_quiet;
    bool too_sparse;
    float mean_rms;
    float voiced_ratio;
};

/**
 * Pure function form of the secondary gate.  Extracted so tests and
 * external tooling can drive it without needing the whole `VoiceListener`
 * infrastructure, and so the orchestration loop stays focused.
 *
 * `effective_frames` is total polled frames minus the tail silence that
 * triggered the end-of-utterance transition.  Must be ≥ 1.
 */
VadGateDecision evaluate_secondary_gate(int voiced_frames,
                                        int effective_frames,
                                        float mean_rms,
                                        float min_utterance_rms,
                                        float min_voiced_frame_ratio);

} // namespace hecquin::voice
