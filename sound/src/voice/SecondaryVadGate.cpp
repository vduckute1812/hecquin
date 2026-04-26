#include "voice/SecondaryVadGate.hpp"

#include "voice/UtteranceCollector.hpp"

#include <algorithm>

namespace hecquin::voice {

VadGateDecision evaluate_secondary_gate(int voiced_frames,
                                        int effective_frames,
                                        float mean_rms,
                                        float min_utterance_rms,
                                        float min_voiced_frame_ratio) {
    const int denom = std::max(1, effective_frames);
    const float voiced_ratio =
        static_cast<float>(voiced_frames) / static_cast<float>(denom);
    const bool too_quiet = mean_rms < min_utterance_rms;
    const bool too_sparse = voiced_ratio < min_voiced_frame_ratio;
    return {!(too_quiet || too_sparse), too_quiet, too_sparse,
            mean_rms, voiced_ratio};
}

VadGateDecision evaluate_for_utterance(const CollectedUtterance& utt,
                                       int poll_interval_ms,
                                       float min_utterance_rms,
                                       float min_voiced_frame_ratio) {
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
    return evaluate_secondary_gate(utt.voiced_frames, effective_frames,
                                   mean_rms, min_utterance_rms,
                                   min_voiced_frame_ratio);
}

} // namespace hecquin::voice
