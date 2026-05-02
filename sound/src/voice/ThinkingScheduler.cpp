#include "voice/ThinkingScheduler.hpp"

#include <utility>

namespace hecquin::voice {

ThinkingScheduler::ThinkingScheduler()
    : worker_([this] { run_(); }) {}

ThinkingScheduler::~ThinkingScheduler() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        state_ = State::ShuttingDown;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void ThinkingScheduler::arm(std::chrono::milliseconds delay,
                            Callback on_fire) {
    if (delay.count() <= 0) {
        // Synchronous fast-path: tests rely on this so they don't have
        // to sleep a real millisecond to observe the callback.
        if (on_fire) on_fire();
        std::lock_guard<std::mutex> lk(mu_);
        fired_ = true;
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        on_fire_ = std::move(on_fire);
        fire_at_ = std::chrono::steady_clock::now() + delay;
        fired_ = false;
        state_ = State::Pending;
    }
    cv_.notify_all();
}

void ThinkingScheduler::cancel() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (state_ == State::Pending) {
            state_ = State::Idle;
            on_fire_ = nullptr;
        }
    }
    cv_.notify_all();
}

bool ThinkingScheduler::fired() const {
    std::lock_guard<std::mutex> lk(mu_);
    return fired_;
}

void ThinkingScheduler::run_() {
    std::unique_lock<std::mutex> lk(mu_);
    while (state_ != State::ShuttingDown) {
        if (state_ != State::Pending) {
            // Idle: wait until armed or shutting down.
            cv_.wait(lk, [this] {
                return state_ == State::Pending ||
                       state_ == State::ShuttingDown;
            });
            continue;
        }

        // Pending: wait until fire_at_ or until cancelled / shutdown.
        const auto fire_at = fire_at_;
        cv_.wait_until(lk, fire_at, [this, fire_at] {
            return state_ != State::Pending ||
                   std::chrono::steady_clock::now() >= fire_at;
        });

        if (state_ != State::Pending) {
            // Cancelled or shutting down — no callback fires.
            continue;
        }
        if (std::chrono::steady_clock::now() < fire_at) {
            // Spurious wake; re-loop to re-evaluate.
            continue;
        }

        Callback cb = std::move(on_fire_);
        on_fire_ = nullptr;
        state_ = State::Idle;
        fired_ = true;
        // Drop the lock around the user callback so a slow `Earcons`
        // invocation doesn't block `cancel()` / `arm()` callers.
        lk.unlock();
        if (cb) cb();
        lk.lock();
    }
}

} // namespace hecquin::voice
