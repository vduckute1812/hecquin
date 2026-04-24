#pragma once

#include <SDL.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

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
 */
class StreamingSdlPlayer {
public:
    ~StreamingSdlPlayer();

    /** Open the device at `sample_rate` Hz.  Returns false on failure. */
    bool start(int sample_rate);

    /** Enqueue a chunk of mono int16 PCM. */
    void push(const std::int16_t* data, std::size_t n_samples);

    /** Mark end-of-stream; starts playback if the prebuffer threshold
     *  was never reached. */
    void finish();

    /** Block until the SDL callback has drained every queued sample. */
    void wait_until_drained();

    /** Close the device and release resources. */
    void stop();

private:
    friend void streaming_callback(void*, Uint8*, int);

    SDL_AudioDeviceID dev_ = 0;
    bool started_ = false;
    std::size_t prebuffer_samples_ = 0;

    std::mutex mu_;
    std::deque<std::int16_t> queue_;
    bool eof_ = false;
    std::atomic<bool> done_{false};
    std::condition_variable not_empty_;
};

} // namespace hecquin::tts::playback
