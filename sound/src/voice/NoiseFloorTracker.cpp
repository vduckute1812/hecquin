#include "voice/NoiseFloorTracker.hpp"

#include <algorithm>
#include <cmath>

namespace hecquin::voice {

namespace {

float clamp_floor(float v, const NoiseFloorConfig& cfg) {
    return std::clamp(v, cfg.min_floor, cfg.max_floor);
}

} // namespace

NoiseFloorTracker::NoiseFloorTracker(NoiseFloorConfig cfg)
    : cfg_(cfg), floor_(clamp_floor(cfg.seed_floor, cfg)) {
    samples_.reserve(static_cast<std::size_t>(std::max(1, cfg_.calibration_samples)));
}

void NoiseFloorTracker::reset() {
    samples_.clear();
    floor_ = clamp_floor(cfg_.seed_floor, cfg_);
    calibrated_ = false;
}

void NoiseFloorTracker::observe(float frame_rms, bool collecting) {
    // Active utterances must not influence the floor.  Anything
    // non-finite is also discarded defensively.
    if (collecting || !std::isfinite(frame_rms) || frame_rms < 0.0f) {
        return;
    }

    if (!calibrated_) {
        samples_.push_back(frame_rms);
        if (static_cast<int>(samples_.size()) >= cfg_.calibration_samples) {
            finish_calibration_();
        }
        return;
    }

    // EMA: floor <- (1-a) * floor + a * sample.
    const float a = std::clamp(cfg_.ema_alpha, 0.0f, 1.0f);
    floor_ = clamp_floor((1.0f - a) * floor_ + a * frame_rms, cfg_);
}

void NoiseFloorTracker::finish_calibration_() {
    if (samples_.empty()) {
        floor_ = clamp_floor(cfg_.seed_floor, cfg_);
        calibrated_ = true;
        return;
    }
    // Median is robust to outliers (a stray cough / door slam during the
    // first second won't anchor the floor far above quiet speech).
    std::sort(samples_.begin(), samples_.end());
    const std::size_t mid = samples_.size() / 2;
    const float med = (samples_.size() % 2 == 1)
                          ? samples_[mid]
                          : 0.5f * (samples_[mid - 1] + samples_[mid]);
    floor_ = clamp_floor(med, cfg_);
    calibrated_ = true;
    // Keep the buffer small once we're done with it.
    samples_.clear();
    samples_.shrink_to_fit();
}

} // namespace hecquin::voice
