#include "learning/ingest/RunBreaker.hpp"

namespace hecquin::learning::ingest {

RunBreaker::RunBreaker(int max_streak, std::optional<Clock::time_point> deadline)
    : max_streak_(max_streak), deadline_(deadline) {}

void RunBreaker::record_success() {
    streak_ = 0;
}

bool RunBreaker::record_failure() {
    if (tripped_) return false;
    ++streak_;
    if (max_streak_ > 0 && streak_ >= max_streak_) {
        tripped_ = true;
        reason_ = "max_consecutive_chunk_failures (" + std::to_string(max_streak_) + ") reached";
        return true;
    }
    return false;
}

bool RunBreaker::check_deadline() {
    if (tripped_ || !deadline_) return false;
    if (Clock::now() < *deadline_) return false;
    tripped_ = true;
    reason_ = "run deadline exceeded";
    return true;
}

} // namespace hecquin::learning::ingest
