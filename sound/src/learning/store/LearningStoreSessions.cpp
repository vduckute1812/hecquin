// Session + interaction + vocab progress operations.
//
// Covers the tables that capture a tutor/learner conversation: `sessions`
// (mode + timing), `interactions` (per-turn transcripts + grammar notes), and
// `vocab_progress` (per-word exposure counts).

#include "learning/store/LearningStore.hpp"

#ifdef HECQUIN_WITH_SQLITE
#include "learning/store/internal/SqliteHelpers.hpp"
#include <sqlite3.h>

#include <cctype>
#endif

namespace hecquin::learning {

#ifdef HECQUIN_WITH_SQLITE
using detail::prepare_or_log;
using detail::step_done;
using detail::Transaction;
using detail::now_epoch_seconds;
#endif

int64_t LearningStore::begin_session(const std::string& mode) {
#ifndef HECQUIN_WITH_SQLITE
    (void)mode;
    return 0;
#else
    if (!db_) return 0;
    auto q = prepare_or_log(db_,
        "INSERT INTO sessions (mode, started_at) VALUES (?, ?);", "session.begin");
    if (!q) return 0;
    sqlite3_bind_text(q.get(), 1, mode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.get(), 2, now_epoch_seconds());
    if (!step_done(db_, q.get(), "session.begin")) return 0;
    return sqlite3_last_insert_rowid(db_);
#endif
}

void LearningStore::end_session(int64_t session_id) {
#ifndef HECQUIN_WITH_SQLITE
    (void)session_id;
#else
    if (!db_ || session_id <= 0) return;
    auto q = prepare_or_log(db_,
        "UPDATE sessions SET ended_at = ? WHERE id = ?;", "session.end");
    if (!q) return;
    sqlite3_bind_int64(q.get(), 1, now_epoch_seconds());
    sqlite3_bind_int64(q.get(), 2, session_id);
    step_done(db_, q.get(), "session.end");
#endif
}

void LearningStore::record_interaction(int64_t session_id,
                                       const std::string& user_text,
                                       const std::string& corrected_text,
                                       const std::string& grammar_notes) {
#ifndef HECQUIN_WITH_SQLITE
    (void)session_id; (void)user_text; (void)corrected_text; (void)grammar_notes;
#else
    if (!db_) return;
    auto q = prepare_or_log(db_,
        "INSERT INTO interactions (session_id, user_text, corrected_text, grammar_notes, created_at) "
        "VALUES (?, ?, ?, ?, ?);",
        "interaction.record");
    if (!q) return;
    if (session_id > 0) {
        sqlite3_bind_int64(q.get(), 1, session_id);
    } else {
        sqlite3_bind_null(q.get(), 1);
    }
    sqlite3_bind_text(q.get(),  2, user_text.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.get(),  3, corrected_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.get(),  4, grammar_notes.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.get(), 5, now_epoch_seconds());
    step_done(db_, q.get(), "interaction.record");
#endif
}

void LearningStore::touch_vocab(const std::vector<std::string>& words) {
#ifndef HECQUIN_WITH_SQLITE
    (void)words;
#else
    if (!db_ || words.empty()) return;
    const int64_t now = now_epoch_seconds();

    Transaction tx(db_);
    if (!tx.active()) return;

    for (const auto& raw : words) {
        std::string w;
        w.reserve(raw.size());
        for (unsigned char c : raw) {
            if (std::isalpha(c) || c == '\'') {
                w.push_back(static_cast<char>(std::tolower(c)));
            }
        }
        if (w.size() < 2) continue;

        auto q = prepare_or_log(db_,
            "INSERT INTO vocab_progress (word, first_seen_at, last_seen_at, seen_count) "
            "VALUES (?, ?, ?, 1) "
            "ON CONFLICT(word) DO UPDATE SET last_seen_at = excluded.last_seen_at, "
            "seen_count = seen_count + 1;",
            "vocab.touch");
        if (!q) return;
        sqlite3_bind_text(q.get(),  1, w.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(q.get(), 2, now);
        sqlite3_bind_int64(q.get(), 3, now);
        if (!step_done(db_, q.get(), "vocab.touch")) return;
    }

    tx.commit();
#endif
}

} // namespace hecquin::learning
