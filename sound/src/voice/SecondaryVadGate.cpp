#include "voice/SecondaryVadGate.hpp"

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

} // namespace hecquin::voice
