#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace hecquin::learning::ingest {

/**
 * Run-wide circuit breaker. Trips on either a sustained chunk-failure streak
 * or a wall-clock deadline; once tripped it stays tripped and exposes a
 * human-readable reason for the orchestrator to log.
 */
class RunBreaker {
public:
    using Clock = std::chrono::steady_clock;

    /** `max_streak <= 0` disables the streak rule; nullopt deadline disables time rule. */
    RunBreaker(int max_streak, std::optional<Clock::time_point> deadline);

    void record_success();

    /** Returns true iff this failure tripped the breaker. */
    bool record_failure();

    /** Trip the breaker if `now() >= deadline`. Returns true iff tripped just now. */
    bool check_deadline();

    bool tripped() const { return tripped_; }
    std::string_view reason() const { return reason_; }

private:
    int max_streak_;
    int streak_ = 0;
    std::optional<Clock::time_point> deadline_;
    bool tripped_ = false;
    std::string reason_;
};

} // namespace hecquin::learning::ingest
