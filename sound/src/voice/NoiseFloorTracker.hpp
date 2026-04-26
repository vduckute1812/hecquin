#pragma once

#include <cstddef>
#include <vector>

namespace hecquin::voice {

/**
 * Tunables for `NoiseFloorTracker`.
 *
 * The tracker has two phases:
 *   1. Calibration: collect the first `calibration_samples` non-collecting
 *      RMS values and use the median (robust to a stray cough or door
 *      slam during boot) as the initial floor.
 *   2. Adaptation: each subsequent non-collecting frame contributes to an
 *      exponential moving average with smoothing factor `ema_alpha`.
 *
 * The output is always clamped to `[min_floor, max_floor]` so that an
 * extremely silent room can't make derived thresholds collapse to zero,
 * and a sustained loud event can't drag them above any plausible voice
 * level.
 */
struct NoiseFloorConfig {
    /** Number of frames to gather before the median is taken. */
    int calibration_samples = 20;
    /** EMA smoothing factor applied to runtime idle frames (0..1). */
    float ema_alpha = 0.05f;
    /** Floor returned before calibration completes (rough silent-room guess). */
    float seed_floor = 0.005f;
    /** Lower clamp for the reported floor. */
    float min_floor = 1e-4f;
    /** Upper clamp for the reported floor. */
    float max_floor = 0.2f;
};

/**
 * Maintains a live estimate of the ambient RMS noise floor.
 *
 * The class is intentionally pure: no SDL, no I/O, no time. Callers feed
 * one RMS value per frame plus a `collecting` flag that tells the
 * tracker whether the current frame is currently inside an active
 * utterance (in which case it must NOT pull the floor up).
 *
 * Derived thresholds (start / continue / min-utterance) are produced by
 * the caller — `NoiseFloorTracker` only owns the floor itself, which
 * keeps it trivially unit-testable.
 */
class NoiseFloorTracker {
public:
    explicit NoiseFloorTracker(NoiseFloorConfig cfg = {});

    /**
     * Feed one frame.  When `collecting` is true the value is ignored so
     * speech can never poison the floor.  During calibration the value
     * is buffered; once `calibration_samples` have been seen the floor
     * is set to their median.  After calibration the EMA takes over.
     */
    void observe(float frame_rms, bool collecting);

    /** Current best-effort noise floor (always within `[min_floor, max_floor]`). */
    float floor() const { return floor_; }

    /** True once the calibration buffer has been processed. */
    bool calibrated() const { return calibrated_; }

    /** Drop the calibration buffer and re-seed.  Useful for tests. */
    void reset();

    /** Test hook: how many calibration samples are buffered. */
    std::size_t calibration_samples_collected() const { return samples_.size(); }

    const NoiseFloorConfig& config() const { return cfg_; }

private:
    void finish_calibration_();

    NoiseFloorConfig cfg_;
    std::vector<float> samples_;
    float floor_;
    bool calibrated_ = false;
};

} // namespace hecquin::voice
