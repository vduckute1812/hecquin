#include "learning/ProgressTracker.hpp"

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

void ProgressTracker::close() {
    if (closed_) return;
    closed_ = true;
    if (store_.is_open() && session_id_ > 0) {
        store_.end_session(session_id_);
    }
}

std::vector<std::string> ProgressTracker::tokenize(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    cur.reserve(24);
    for (unsigned char c : text) {
        if (std::isalpha(c) || c == '\'') {
            cur.push_back(static_cast<char>(std::tolower(c)));
        } else {
            if (cur.size() >= 2) out.push_back(cur);
            cur.clear();
        }
    }
    if (cur.size() >= 2) out.push_back(std::move(cur));
    return out;
}

} // namespace hecquin::learning
