#include "voice/SecondaryVadGate.hpp"

#include "voice/UtteranceCollector.hpp"

#include <algorithm>
#include <cmath>

namespace hecquin::voice {

namespace {

/**
 * peak_amplitude / mean_rms.  Speech is peaky (crest factor ~8-15),
 * white-ish noise sits around 3-5, constant tones near 1.4.  Returns
 * 0 for empty input or when `mean_rms` is too small to divide safely.
 */
float compute_crest_factor(const std::vector<float>& pcm, float mean_rms) {
    if (pcm.empty() || mean_rms <= 1e-6f) return 0.0f;
    float peak = 0.0f;
    for (float s : pcm) {
        const float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    return peak / mean_rms;
}

} // namespace

VadGateDecision evaluate_secondary_gate(int voiced_frames,
                                        int effective_frames,
                                        float mean_rms,
                                        float min_utterance_rms,
                                        float min_voiced_frame_ratio,
                                        float crest_factor,
                                        float min_crest_factor) {
    const int denom = std::max(1, effective_frames);
    const float voiced_ratio =
        static_cast<float>(voiced_frames) / static_cast<float>(denom);
    const bool too_quiet = mean_rms < min_utterance_rms;
    const bool too_sparse = voiced_ratio < min_voiced_frame_ratio;
    // The crest-factor check is opt-in: callers that don't supply a
    // PCM-derived value (or set the floor to 0) keep the legacy
    // two-knob behaviour byte-for-byte.
    const bool too_flat =
        min_crest_factor > 0.0f && crest_factor > 0.0f &&
        crest_factor < min_crest_factor;
    VadGateDecision d;
    d.accept = !(too_quiet || too_sparse || too_flat);
    d.too_quiet = too_quiet;
    d.too_sparse = too_sparse;
    d.too_flat = too_flat;
    d.mean_rms = mean_rms;
    d.voiced_ratio = voiced_ratio;
    d.crest_factor = crest_factor;
    return d;
}

VadGateDecision evaluate_for_utterance(const CollectedUtterance& utt,
                                       int poll_interval_ms,
                                       float min_utterance_rms,
                                       float min_voiced_frame_ratio,
                                       float min_crest_factor) {
    // Subtract the terminal silence tail from the denominator so short
    // utterances aren't unfairly penalised by the trailing end-silence
    // window that closed them.
    const int tail_silence_frames =
        poll_interval_ms > 0 ? utt.silence_ms / poll_interval_ms : 0;
    const int effective_frames =
        std::max(1, utt.total_frames - tail_silence_frames);
    const float mean_rms =
        utt.pcm.empty()
            ? 0.0f
            : UtteranceCollector::rms(utt.pcm, 0, utt.pcm.size());
    const float crest = compute_crest_factor(utt.pcm, mean_rms);
    return evaluate_secondary_gate(utt.voiced_frames, effective_frames,
                                   mean_rms, min_utterance_rms,
                                   min_voiced_frame_ratio,
                                   crest, min_crest_factor);
}

} // namespace hecquin::voice
