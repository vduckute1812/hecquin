#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace hecquin::voice {

/**
 * One re-armable timer worker that fires a callback after a delay
 * unless cancelled.  Replaces the per-utterance `std::thread + cv`
 * dance that used to live inline at the top of
 * `VoiceListener::process_utterance_`.
 *
 * Design goals:
 *
 *   - Zero allocations per `arm()` — the worker thread is spawned once
 *     in the constructor and lives for the listener's whole lifetime.
 *   - `arm(delay, on_fire)` is non-blocking; the caller resumes
 *     immediately while the worker waits.  A second `arm` while a
 *     timer is already pending replaces the previous request.
 *   - `cancel()` is idempotent and safe before / after the timer has
 *     fired.  When the timer hadn't fired yet, the callback never
 *     runs; when it had, `cancel()` is just a no-op (the caller can
 *     still issue any "stop" follow-up — e.g. `Earcons::stop_thinking()`).
 *   - Destruction joins the worker cleanly even if a timer is pending.
 *
 * Thread-safety: `arm`, `cancel`, `fired`, and the destructor are all
 * safe to call from any thread.  The fire callback runs on the
 * worker thread; callers must not assume any particular thread
 * affinity inside the callback.
 */
class ThinkingScheduler {
public:
    using Callback = std::function<void()>;

    ThinkingScheduler();
    ~ThinkingScheduler();

    ThinkingScheduler(const ThinkingScheduler&) = delete;
    ThinkingScheduler& operator=(const ThinkingScheduler&) = delete;

    /**
     * Schedule `on_fire` to run after `delay`.  Replaces any
     * previously-armed (and not-yet-fired) timer.  When `delay <= 0`
     * the callback is invoked synchronously on the calling thread
     * before `arm` returns — useful for tests.
     */
    void arm(std::chrono::milliseconds delay, Callback on_fire);

    /** Cancel a pending timer.  No-op if no timer is pending. */
    void cancel();

    /** Returns true if the most recently-armed timer's callback ran. */
    bool fired() const;

private:
    void run_();

    enum class State { Idle, Pending, ShuttingDown };

    mutable std::mutex mu_;
    std::condition_variable cv_;
    State state_ = State::Idle;
    bool fired_ = false;
    std::chrono::steady_clock::time_point fire_at_{};
    Callback on_fire_;
    std::thread worker_;
};

} // namespace hecquin::voice
