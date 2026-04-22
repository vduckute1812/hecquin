#pragma once

#include "learning/store/LearningStore.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hecquin::learning {

/**
 * Thin façade over LearningStore for session bookkeeping. A single instance binds to
 * one active session; destruction automatically closes it.
 */
class ProgressTracker {
public:
    ProgressTracker(LearningStore& store, std::string mode);
    ~ProgressTracker();

    ProgressTracker(const ProgressTracker&) = delete;
    ProgressTracker& operator=(const ProgressTracker&) = delete;

    int64_t session_id() const { return session_id_; }

    /** Record one tutor round-trip and bump vocab counters for each token in `user_text`. */
    void log_interaction(const std::string& user_text,
                         const std::string& corrected_text,
                         const std::string& grammar_notes);

    /** Record one pronunciation / intonation drill attempt + phoneme-mastery roll-up. */
    void log_pronunciation(const std::string& reference,
                           const std::string& transcript,
                           float pron_overall_0_100,
                           float intonation_overall_0_100,
                           const std::string& per_phoneme_json,
                           const std::vector<std::pair<std::string, float>>& per_phoneme_scores);

    /** Manually finalize; also called from the destructor. Idempotent. */
    void close();

    /** Tokenise `text` into lowercase alpha tokens (length >= 2). */
    static std::vector<std::string> tokenize(const std::string& text);

private:
    LearningStore& store_;
    int64_t session_id_ = 0;
    bool closed_ = false;
};

} // namespace hecquin::learning
