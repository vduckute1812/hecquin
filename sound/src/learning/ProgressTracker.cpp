#include "learning/ProgressTracker.hpp"

#include "learning/Vocabulary.hpp"

#include <cctype>

namespace hecquin::learning {

ProgressTracker::ProgressTracker(LearningStore& store, std::string mode) : store_(store) {
    if (store_.is_open()) {
        session_id_ = store_.begin_session(mode);
    }
}

ProgressTracker::~ProgressTracker() {
    close();
}

void ProgressTracker::log_interaction(const std::string& user_text,
                                      const std::string& corrected_text,
                                      const std::string& grammar_notes) {
    if (!store_.is_open()) return;
    store_.record_interaction(session_id_, user_text, corrected_text, grammar_notes);
    store_.touch_vocab(tokenize(user_text));
}

void ProgressTracker::log_pronunciation(
    const std::string& reference,
    const std::string& transcript,
    float pron_overall_0_100,
    float intonation_overall_0_100,
    const std::string& per_phoneme_json,
    const std::vector<std::pair<std::string, float>>& per_phoneme_scores) {
    if (!store_.is_open()) return;
    store_.record_pronunciation_attempt(session_id_, reference, transcript,
                                        pron_overall_0_100,
                                        intonation_overall_0_100,
                                        per_phoneme_json);
    store_.touch_phoneme_mastery(per_phoneme_scores);
}

void ProgressTracker::close() {
    if (closed_) return;
    closed_ = true;
    if (store_.is_open() && session_id_ > 0) {
        store_.end_session(session_id_);
    }
}

std::vector<std::string> ProgressTracker::tokenize(const std::string& text) {
    // Split on any non-word byte, then hand each run to the shared
    // normaliser so tokenize/touch_vocab agree on the character class.
    std::vector<std::string> out;
    std::string cur;
    cur.reserve(24);
    auto flush = [&] {
        if (!cur.empty()) {
            const std::string norm = Vocabulary::normalise(cur);
            if (norm.size() >= 2) out.push_back(norm);
            cur.clear();
        }
    };
    for (unsigned char c : text) {
        if (std::isalpha(c) || c == '\'') {
            cur.push_back(static_cast<char>(c));
        } else {
            flush();
        }
    }
    flush();
    return out;
}

} // namespace hecquin::learning
