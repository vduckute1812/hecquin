#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace hecquin::tts::playback {

/**
 * Producer/consumer mono int16 PCM ring queue with cv-backed drain
 * signalling.
 *
 * The previous in-place implementation inside `StreamingSdlPlayer` had
 * a `std::condition_variable not_empty_` member that was never used —
 * `wait_until_drained` busy-slept in 20 ms slices instead.  Lifting the
 * queue here lets the cv become the actual signalling primitive: the
 * SDL callback wakes a single `wait_until_drained` waiter as soon as
 * `mark_eof()` has been called *and* the queue has been fully drained
 * by `pop_into`.
 *
 * Thread model:
 *   - Single producer (the synth side calls `push`).
 *   - Single consumer (the SDL audio callback calls `pop_into`).
 *   - The waiter (`wait_until_drained`) lives on whichever thread
 *     started playback; it never inserts/removes samples.
 */
class PcmRingQueue {
public:
    /** Append `n_samples` int16 values to the queue. */
    void push(const std::int16_t* data, std::size_t n_samples);

    /**
     * Pop up to `byte_len` bytes into `dst` (treated as a contiguous
     * int16 array).  Any shortfall is zero-padded so the SDL callback
     * always receives a well-defined buffer.  Notifies a `wait_until_
     * drained` waiter when the queue becomes empty after EOF.
     */
    void pop_into(std::uint8_t* dst, int byte_len);

    /** Mark end-of-stream.  Notifies a `wait_until_drained` waiter so
     *  it can recheck the drain condition without blocking forever. */
    void mark_eof();

    /** Block until the queue has reached EOF *and* been fully drained
     *  by `pop_into`.  Wakes via cv — no busy-sleep. */
    void wait_until_drained();

    /** Snapshot of the queued sample count.  Used by the prebuffer
     *  threshold check in `StreamingSdlPlayer::push`. */
    std::size_t size() const;

    /** Snapshot of `eof_ && queue_.empty()`. */
    bool drained() const;

private:
    mutable std::mutex      mu_;
    std::deque<std::int16_t> queue_;
    bool                    eof_ = false;
    std::condition_variable drained_cv_;
};

} // namespace hecquin::tts::playback
