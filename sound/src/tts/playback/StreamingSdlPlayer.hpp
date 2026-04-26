#pragma once

#include "tts/playback/PcmRingQueue.hpp"
#include "tts/playback/SdlMonoDevice.hpp"

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

private:
    static void sdl_callback_(void* userdata, std::uint8_t* stream, int len);

    SdlMonoDevice device_;
    PcmRingQueue  queue_;
    bool          started_ = false;
    std::size_t   prebuffer_samples_ = 0;
};

} // namespace hecquin::tts::playback
