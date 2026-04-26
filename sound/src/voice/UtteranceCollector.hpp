#pragma once

#include "voice/NoiseFloorTracker.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

class AudioCapture;
struct VoiceListenerConfig;

namespace hecquin::voice {

/** Raw stats + PCM for one end-of-utterance event, ready for secondary-gate /
 *  Whisper. */
struct CollectedUtterance {
    std::vector<float> pcm;
    int voiced_frames = 0;
    int total_frames = 0;
    int speech_ms = 0;
    int silence_ms = 0;
};

/**
 * Poll-loop collector.  Owns the 50 ms poll cadence, primary VAD,
 * collection timers, and voiced-ratio counters that used to live in
 * `VoiceListener::run()`.  Each call to `collect_next()` blocks until
 * either:
 *   - an utterance with `speech_ms ≥ min_speech_ms` and a trailing
 *     `silence_ms ≥ end_silence_ms` is observed, or
 *   - `app_running` goes false (returns `std::nullopt`).
 *
 * Separating this out lets the listener test the collection loop in
 * isolation, and keeps `VoiceListener` focused on dispatch +
 * observability.
 */
class UtteranceCollector {
public:
    UtteranceCollector(AudioCapture& capture,
                       const VoiceListenerConfig& cfg,
                       const std::atomic<bool>& app_running);

    std::optional<CollectedUtterance> collect_next();

    /** RMS over [start, end).  Exposed so the listener can run the
     *  same calculation over the full utterance buffer. */
    static float rms(const std::vector<float>& samples,
                     std::size_t start, std::size_t end);

    /**
     * Threshold the secondary VAD gate should compare the utterance's
     * mean RMS against.  When auto-calibration is on this tracks the
     * adaptive value derived from the live noise floor; otherwise it
     * is the static `cfg.min_utterance_rms`.
     *
     * Used by `VoiceListener::run` to keep the gate's per-tick decision
     * in lockstep with the collector's per-frame decision.
     */
    float effective_min_utterance_rms() const { return dynamic_min_utt_rms_; }

    /** Threshold currently used to start a new utterance. */
    float effective_voice_rms_threshold() const { return dynamic_start_thr_; }

    /** Threshold used to keep an in-progress utterance alive (hysteresis). */
    float effective_continue_threshold() const { return dynamic_continue_thr_; }

    /** Live noise floor (post-clamp). */
    float current_noise_floor() const { return tracker_.floor(); }

    /** True once the noise-floor calibration window has completed. */
    bool calibrated() const { return tracker_.calibrated(); }

    /**
     * Mark the speakers as actively producing audio that bleeds back into
     * the microphone (e.g. while a song is streaming).  While set, the
     * noise-floor tracker stops absorbing per-frame RMS samples so the
     * loud playback can't drag the dynamic start threshold up to "loud
     * music"-sized values that would refuse to detect normal speech once
     * the music stops.  Toggle off the moment playback is aborted /
     * paused so the floor resumes tracking real ambient noise.
     */
    void set_external_audio_active(bool active) {
        external_audio_active_.store(active, std::memory_order_release);
    }

    /**
     * Drop the calibrated floor and force a fresh calibration window.
     * Used after `set_external_audio_active(false)` so the listener
     * does not have to wait for the EMA to bleed off whatever residual
     * the music left in the floor — the next `calibration_ms` of
     * non-collecting frames simply re-establish a clean ambient.
     */
    void reset_noise_floor() {
        tracker_.reset();
        announced_calibration_done_ = false;
    }

private:
    /** Reason `collect_next()` decided to close the current utterance. */
    enum class EndReason {
        None,        ///< Still collecting / not in collecting state.
        Silence,     ///< speech_ms ≥ min and silence_ms ≥ end_silence_ms.
        MaxDuration, ///< speech_ms exceeded `cfg_.max_utterance_ms`.
    };

    void recompute_thresholds_();
    /** Feed the noise tracker (skipped during speech, freezes after
     *  calibration if `auto_adapt=false`, and skipped while external
     *  audio is bleeding through the mic). */
    void update_floor_estimate_(float frame_rms, bool collecting);
    /** Print "🎯 Calibrated …" exactly once when calibration completes. */
    void announce_calibration_once_();
    /** Rate-limited (~1 Hz) "[vad] floor= …" trace when `cfg_.debug`. */
    void log_vad_debug_(float frame_rms, bool collecting);
    /** Per-frame VAD decision with hysteresis (continue threshold while
     *  collecting) and a calibration gate that blocks new triggers
     *  before the noise floor is established. */
    bool detect_voice_(float frame_rms, bool window_ready, bool collecting) const;
    /** Bump collection counters for one poll tick.  `u.silence_ms` is
     *  used as the running silence counter (reset on voiced frames). */
    void advance_collection_(CollectedUtterance& u, bool has_voice) const;
    /** Decide whether `collect_next()` should end this tick. */
    EndReason end_reason_(const CollectedUtterance& u) const;
    /** Print the closing emoji line for the chosen end reason. */
    void announce_recording_complete_(EndReason reason) const;

    AudioCapture& capture_;
    const VoiceListenerConfig& cfg_;
    const std::atomic<bool>& app_running_;
    NoiseFloorTracker tracker_;
    float dynamic_start_thr_;
    float dynamic_continue_thr_;
    float dynamic_min_utt_rms_;
    std::chrono::steady_clock::time_point last_debug_log_{};
    /**
     * Set once the calibration window completes so we print the
     * "🎯 Calibrated" handshake exactly once per process — even though
     * `recompute_thresholds_()` runs every poll tick.
     */
    bool announced_calibration_done_ = false;
    /**
     * Toggled by `set_external_audio_active` from another thread (the
     * voice-listener side-effect handler).  Atomic so the poll loop
     * sees changes promptly without taking a lock.
     */
    std::atomic<bool> external_audio_active_{false};
};

} // namespace hecquin::voice
