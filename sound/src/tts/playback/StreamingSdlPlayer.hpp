#pragma once

#include "tts/playback/PcmRingQueue.hpp"
#include "tts/playback/SdlMonoDevice.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hecquin::tts::playback {

/**
 * Producer/consumer SDL player.
 *
 * Typical use:
 *
 *     StreamingSdlPlayer player;
 *     if (!player.start(22050)) return false;
 *     while (have_more) player.push(pcm, n);
 *     player.finish();
 *     player.wait_until_drained();
 *
 * The reader side pushes int16 chunks as they become available; the
 * SDL audio callback drains them.  Playback auto-starts once the queue
 * hits a ~60 ms prebuffer threshold (so very short utterances still
 * play even if they never reach the threshold — `finish()` force-starts
 * the device in that case).
 *
 * Internally this is a thin facade over `PcmRingQueue` (cv-backed
 * queue + drain signalling) and `SdlMonoDevice` (RAII handle around
 * `SDL_OpenAudioDevice`).  Both are exposed in their own headers so
 * tests can exercise the queue without bringing up SDL.
 */
class StreamingSdlPlayer {
public:
    StreamingSdlPlayer() = default;
    ~StreamingSdlPlayer();

    StreamingSdlPlayer(const StreamingSdlPlayer&) = delete;
    StreamingSdlPlayer& operator=(const StreamingSdlPlayer&) = delete;

    /** Open the device at `sample_rate` Hz.  Returns false on failure. */
    bool start(int sample_rate);

    /** Enqueue a chunk of mono int16 PCM. */
    void push(const std::int16_t* data, std::size_t n_samples);

    /** Mark end-of-stream; starts playback if the prebuffer threshold
     *  was never reached. */
    void finish();

    /** Block until the SDL callback has drained every queued sample. */
    void wait_until_drained();

    /**
     * Suspend / resume the SDL audio callback without releasing the
     * device.  Idempotent.  Used by the music path so "pause music" /
     * "continue music" voice intents can flip the stream without
     * tearing down `yt-dlp` + `ffmpeg`.  Returns false when the device
     * is not open (start() has not been called yet, or stop() already
     * ran) so callers can detect a no-op.
     */
    bool set_paused(bool paused);

    /** Close the device and release resources. */
    void stop();

    /**
     * Set the desired output gain (linear, 0..1+).  The audio callback
     * slews `current_gain_` toward the new target over `ramp_ms` so
     * the user never hears a click.  Thread-safe; callable from any
     * thread.  Default is 1.0 (no attenuation, no boost).
     *
     * Used by `voice::AudioBargeInController` to duck the music
     * player when incoming voice is detected.
     */
    void set_gain_target(float linear, int ramp_ms);

    /** Last applied target (linear).  Mostly for tests. */
    float gain_target() const {
        return target_gain_.load(std::memory_order_acquire);
    }

    /**
     * Pure (test-friendly) gain helper.  Walks `samples` in-place
     * applying `current_gain` per sample and slewing it toward
     * `target` so the cumulative ramp covers `ramp_ms` of audio at
     * `sample_rate` Hz.  `current_gain`, `ramp_step` and
     * `ramp_for_target` are state variables the caller persists
     * between buffers (the SDL callback owns them; tests pass their
     * own copies).
     *
     * Static so the test binary can drive it without instantiating
     * an SDL device.  No side effects beyond the four out-params.
     */
    static void apply_gain(std::int16_t* samples, std::size_t n,
                           int sample_rate, float target, int ramp_ms,
                           float& current_gain,
                           float& ramp_step,
                           float& ramp_for_target);

private:
    static void sdl_callback_(void* userdata, std::uint8_t* stream, int len);

    /** Per-callback wrapper around `apply_gain` that uses the
     *  member-owned slewing state. */
    void apply_gain_(std::int16_t* samples, std::size_t n);

    SdlMonoDevice device_;
    PcmRingQueue  queue_;
    bool          started_ = false;
    std::size_t   prebuffer_samples_ = 0;
    int           sample_rate_ = 0;

    /**
     * Linear gain state.  `target_gain_` and `target_ramp_ms_` are
     * written from any thread via `set_gain_target`; the audio
     * callback re-reads them every buffer and updates the local
     * `current_gain_` / `ramp_step_` it owns exclusively.  No locks:
     * a missed target on one buffer is corrected on the next.
     */
    std::atomic<float> target_gain_{1.0f};
    std::atomic<int>   target_ramp_ms_{0};
    float              current_gain_ = 1.0f;
    /** Cached per-sample step the callback uses while ramping. */
    float              ramp_step_    = 0.0f;
    /** Snapshot of the target the current `ramp_step_` was computed
     *  for; used to detect a target change between buffers. */
    float              ramp_for_target_ = 1.0f;
};

} // namespace hecquin::tts::playback
