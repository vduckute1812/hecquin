#pragma once

#include <cstddef>
#include <vector>

namespace hecquin::learning::prosody {

struct PitchTrackerConfig {
    int sample_rate = 16000;      ///< Hz
    int frame_hop_samples = 160;  ///< 10 ms at 16 kHz
    int frame_size_samples = 1024;///< YIN window
    float min_f0_hz = 70.0f;      ///< adult-female range floor
    float max_f0_hz = 500.0f;     ///< adult-female range ceiling
    float voicing_threshold = 0.15f;  ///< YIN cumulative-difference threshold (unvoiced above)
};

struct PitchContour {
    std::vector<float> f0_hz;     ///< 0.0 marks unvoiced frames
    std::vector<float> rms;       ///< per-frame energy (same length as f0_hz)
    float frame_hop_ms = 10.0f;
    int sample_rate = 16000;

    [[nodiscard]] std::size_t num_frames() const { return f0_hz.size(); }
    [[nodiscard]] bool empty() const { return f0_hz.empty(); }
};

/**
 * Pure-C++ YIN fundamental-frequency tracker (de Cheveigné & Kawahara, 2002).
 *
 * Produces a per-frame F0 estimate (0 Hz = unvoiced) plus an RMS contour so
 * downstream intonation scoring can weight frames by energy.  Resolution is
 * controlled by `frame_hop_samples` (10 ms default).
 *
 * Not the fastest implementation possible — this is sized for utterances
 * ≤ 10 s on a Raspberry Pi, which a single-threaded O(N · W) pass handles
 * comfortably (~200k ops/frame · 1000 frames = ~200M ops total).
 */
class PitchTracker {
public:
    explicit PitchTracker(PitchTrackerConfig cfg = {});

    [[nodiscard]] PitchContour track(const std::vector<float>& pcm) const;

    [[nodiscard]] const PitchTrackerConfig& config() const { return cfg_; }

private:
    PitchTrackerConfig cfg_;
};

} // namespace hecquin::learning::prosody
