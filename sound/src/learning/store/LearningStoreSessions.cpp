// Session + interaction + vocab progress operations.
//
// Covers the tables that capture a tutor/learner conversation: `sessions`
// (mode + timing), `interactions` (per-turn transcripts + grammar notes), and
// `vocab_progress` (per-word exposure counts).  The TU exposes free
// functions in `hecquin::learning::store::detail` and thin forwarders on
// `LearningStore` (Option A of the store refactor).

#include "learning/store/LearningStore.hpp"

#ifdef HECQUIN_WITH_SQLITE
#include "learning/Vocabulary.hpp"
#include "learning/store/detail/SessionsOps.hpp"
#include "learning/store/internal/SqliteHelpers.hpp"
#include <sqlite3.h>
#endif

namespace hecquin::learning {

#ifdef HECQUIN_WITH_SQLITE

namespace store::detail {

int64_t begin_session(sqlite3* db,
                      learning::detail::StatementCache& cache,
                      const std::string& mode) {
    using learning::detail::step_done;
    using learning::detail::now_epoch_seconds;
    if (!db) return 0;
    auto q = cache.acquire("session.begin",
        "INSERT INTO sessions (mode, started_at) VALUES (?, ?);");
    if (!q) return 0;
    sqlite3_bind_text(q.get(), 1, mode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.get(), 2, now_epoch_seconds());
    if (!step_done(db, q.get(), "session.begin")) return 0;
    return sqlite3_last_insert_rowid(db);
}

void end_session(sqlite3* db,
                 learning::detail::StatementCache& cache,
                 int64_t session_id) {
    using learning::detail::step_done;
    using learning::detail::now_epoch_seconds;
    if (!db || session_id <= 0) return;
    auto q = cache.acquire("session.end",
        "UPDATE sessions SET ended_at = ? WHERE id = ?;");
    if (!q) return;
    sqlite3_bind_int64(q.get(), 1, now_epoch_seconds());
    sqlite3_bind_int64(q.get(), 2, session_id);
    step_done(db, q.get(), "session.end");
}

void record_interaction(sqlite3* db,
                        learning::detail::StatementCache& cache,
                        int64_t session_id,
                        const std::string& user_text,
                        const std::string& corrected_text,
                        const std::string& grammar_notes) {
    using learning::detail::step_done;
    using learning::detail::now_epoch_seconds;
    if (!db) return;
    auto q = cache.acquire("interaction.record",
        "INSERT INTO interactions (session_id, user_text, corrected_text, grammar_notes, created_at) "
        "VALUES (?, ?, ?, ?, ?);");
    if (!q) return;
    if (session_id > 0) sqlite3_bind_int64(q.get(), 1, session_id);
    else                sqlite3_bind_null(q.get(), 1);
    sqlite3_bind_text(q.get(),  2, user_text.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.get(),  3, corrected_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.get(),  4, grammar_notes.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.get(), 5, now_epoch_seconds());
    step_done(db, q.get(), "interaction.record");
}

void touch_vocab(sqlite3* db,
                 learning::detail::StatementCache& cache,
                 const std::vector<std::string>& words) {
    using learning::detail::step_done;
    using learning::detail::Transaction;
    using learning::detail::now_epoch_seconds;
    if (!db || words.empty()) return;
    const int64_t now = now_epoch_seconds();

    Transaction tx(db);
    if (!tx.active()) return;

    for (const auto& raw : words) {
        const std::string w = hecquin::learning::Vocabulary::normalise(raw);
        if (w.size() < 2) continue;

        auto q = cache.acquire("vocab.touch",
            "INSERT INTO vocab_progress (word, first_seen_at, last_seen_at, seen_count) "
            "VALUES (?, ?, ?, 1) "
            "ON CONFLICT(word) DO UPDATE SET last_seen_at = excluded.last_seen_at, "
            "seen_count = seen_count + 1;");
        if (!q) return;
        sqlite3_bind_text(q.get(),  1, w.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(q.get(), 2, now);
        sqlite3_bind_int64(q.get(), 3, now);
        if (!step_done(db, q.get(), "vocab.touch")) return;
    }

    tx.commit();
}

} // namespace store::detail

#endif // HECQUIN_WITH_SQLITE

int64_t LearningStore::begin_session(const std::string& mode) {
#ifndef HECQUIN_WITH_SQLITE
    (void)mode;
    return 0;
#else
    if (!stmt_cache_) return 0;
    return store::detail::begin_session(db_, *stmt_cache_, mode);
#endif
}

void LearningStore::end_session(int64_t session_id) {
#ifndef HECQUIN_WITH_SQLITE
    (void)session_id;
#else
    if (!stmt_cache_) return;
    store::detail::end_session(db_, *stmt_cache_, session_id);
#endif
}

void LearningStore::record_interaction(int64_t session_id,
                                       const std::string& user_text,
                                       const std::string& corrected_text,
                                       const std::string& grammar_notes) {
#ifndef HECQUIN_WITH_SQLITE
    (void)session_id; (void)user_text; (void)corrected_text; (void)grammar_notes;
#else
    if (!stmt_cache_) return;
    store::detail::record_interaction(db_, *stmt_cache_, session_id,
                                       user_text, corrected_text, grammar_notes);
#endif
}

void LearningStore::touch_vocab(const std::vector<std::string>& words) {
#ifndef HECQUIN_WITH_SQLITE
    (void)words;
#else
    if (!stmt_cache_) return;
    store::detail::touch_vocab(db_, *stmt_cache_, words);
#endif
}

} // namespace hecquin::learning
