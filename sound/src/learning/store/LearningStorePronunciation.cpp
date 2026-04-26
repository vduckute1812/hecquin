// Pronunciation drill + phoneme mastery operations.
//
// `pronunciation_attempts` is append-only (one row per drill); scores and
// per-phoneme JSON are stored opaquely so the schema can outlive tweaks to
// the scoring code. `phoneme_mastery` is a rolling average keyed by IPA.

#include "learning/store/LearningStore.hpp"

#ifdef HECQUIN_WITH_SQLITE
#include "learning/store/detail/PronunciationOps.hpp"
#include "learning/store/internal/SqliteHelpers.hpp"
#include <sqlite3.h>

#include <algorithm>
#endif

namespace hecquin::learning {

#ifdef HECQUIN_WITH_SQLITE

namespace store::detail {

void record_pronunciation_attempt(sqlite3* db,
                                  learning::detail::StatementCache& cache,
                                  int64_t session_id,
                                  const std::string& reference,
                                  const std::string& transcript,
                                  float pron_overall_0_100,
                                  float intonation_overall_0_100,
                                  const std::string& per_phoneme_json) {
    using learning::detail::step_done;
    using learning::detail::now_epoch_seconds;
    if (!db) return;
    auto q = cache.acquire("pron.record",
        "INSERT INTO pronunciation_attempts (session_id, reference, transcript, "
        "  pron_overall, intonation_overall, per_phoneme_json, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);");
    if (!q) return;
    if (session_id > 0) sqlite3_bind_int64(q.get(), 1, session_id);
    else                sqlite3_bind_null(q.get(), 1);
    sqlite3_bind_text(q.get(), 2, reference.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.get(), 3, transcript.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(q.get(), 4, static_cast<double>(pron_overall_0_100));
    sqlite3_bind_double(q.get(), 5, static_cast<double>(intonation_overall_0_100));
    sqlite3_bind_text(q.get(), 6, per_phoneme_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.get(), 7, now_epoch_seconds());
    step_done(db, q.get(), "pron.record");
}

void touch_phoneme_mastery(sqlite3* db,
                           learning::detail::StatementCache& cache,
                           const std::vector<std::pair<std::string, float>>& scored) {
    using learning::detail::step_done;
    using learning::detail::Transaction;
    using learning::detail::now_epoch_seconds;
    if (!db || scored.empty()) return;
    const int64_t now = now_epoch_seconds();

    Transaction tx(db);
    if (!tx.active()) return;

    for (const auto& [ipa, score] : scored) {
        if (ipa.empty()) continue;
        auto q = cache.acquire("phoneme.touch",
            "INSERT INTO phoneme_mastery (ipa, attempts, avg_score, last_seen_at) "
            "VALUES (?, 1, ?, ?) "
            "ON CONFLICT(ipa) DO UPDATE SET "
            "  avg_score = (avg_score * attempts + excluded.avg_score) / (attempts + 1), "
            "  attempts = attempts + 1, "
            "  last_seen_at = excluded.last_seen_at;");
        if (!q) return;
        sqlite3_bind_text(q.get(), 1, ipa.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(q.get(), 2, static_cast<double>(score));
        sqlite3_bind_int64(q.get(), 3, now);
        if (!step_done(db, q.get(), "phoneme.touch")) return;
    }
    tx.commit();
}

std::vector<std::string> weakest_phonemes(sqlite3* db,
                                          learning::detail::StatementCache& cache,
                                          int n,
                                          int min_attempts) {
    std::vector<std::string> out;
    if (!db || n <= 0) return out;
    auto q = cache.acquire("phoneme.weakest",
        "SELECT ipa FROM phoneme_mastery "
        "WHERE attempts >= ? "
        "ORDER BY avg_score ASC, attempts DESC "
        "LIMIT ?;");
    if (!q) return out;
    sqlite3_bind_int(q.get(), 1, std::max(1, min_attempts));
    sqlite3_bind_int(q.get(), 2, n);
    out.reserve(static_cast<std::size_t>(n));
    for (;;) {
        const int rc = sqlite3_step(q.get());
        if (rc == SQLITE_ROW) {
            const unsigned char* t = sqlite3_column_text(q.get(), 0);
            if (t) out.emplace_back(reinterpret_cast<const char*>(t));
        } else {
            break;
        }
    }
    return out;
}

} // namespace store::detail

#endif // HECQUIN_WITH_SQLITE

void LearningStore::record_pronunciation_attempt(int64_t session_id,
                                                 const std::string& reference,
                                                 const std::string& transcript,
                                                 float pron_overall_0_100,
                                                 float intonation_overall_0_100,
                                                 const std::string& per_phoneme_json) {
#ifndef HECQUIN_WITH_SQLITE
    (void)session_id; (void)reference; (void)transcript;
    (void)pron_overall_0_100; (void)intonation_overall_0_100; (void)per_phoneme_json;
#else
    if (!stmt_cache_) return;
    store::detail::record_pronunciation_attempt(
        db_, *stmt_cache_, session_id, reference, transcript,
        pron_overall_0_100, intonation_overall_0_100, per_phoneme_json);
#endif
}

void LearningStore::touch_phoneme_mastery(
    const std::vector<std::pair<std::string, float>>& scored) {
#ifndef HECQUIN_WITH_SQLITE
    (void)scored;
#else
    if (!stmt_cache_) return;
    store::detail::touch_phoneme_mastery(db_, *stmt_cache_, scored);
#endif
}

std::vector<std::string> LearningStore::weakest_phonemes(int n, int min_attempts) const {
#ifndef HECQUIN_WITH_SQLITE
    (void)n; (void)min_attempts;
    return {};
#else
    if (!stmt_cache_) return {};
    return store::detail::weakest_phonemes(db_, *stmt_cache_, n, min_attempts);
#endif
}

std::optional<int64_t>
LearningStore::upsert_user(const std::string& display_name) {
#ifndef HECQUIN_WITH_SQLITE
    (void)display_name;
    return std::nullopt;
#else
    if (!db_ || !stmt_cache_ || display_name.empty()) return std::nullopt;
    using learning::detail::now_epoch_seconds;
    {
        auto q = stmt_cache_->acquire("users.upsert",
            "INSERT INTO users (display_name, created_at) VALUES (?, ?) "
            "ON CONFLICT(display_name) DO NOTHING;");
        if (!q) return std::nullopt;
        sqlite3_bind_text(q.get(), 1, display_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(q.get(), 2, now_epoch_seconds());
        if (sqlite3_step(q.get()) != SQLITE_DONE) return std::nullopt;
    }
    auto q = stmt_cache_->acquire("users.lookup",
        "SELECT id FROM users WHERE display_name = ? LIMIT 1;");
    if (!q) return std::nullopt;
    sqlite3_bind_text(q.get(), 1, display_name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(q.get()) != SQLITE_ROW) return std::nullopt;
    return sqlite3_column_int64(q.get(), 0);
#endif
}

std::optional<float>
LearningStore::last_session_pronunciation_score(
    int limit, std::optional<int64_t> user_id) const {
#ifndef HECQUIN_WITH_SQLITE
    (void)limit; (void)user_id;
    return std::nullopt;
#else
    if (!db_ || !stmt_cache_ || limit <= 0) return std::nullopt;
    const char* sql = user_id
        ? "SELECT AVG(pron_overall) FROM ("
          "  SELECT pron_overall FROM pronunciation_attempts "
          "  WHERE pron_overall IS NOT NULL AND user_id = ? "
          "  ORDER BY id DESC LIMIT ?)"
        : "SELECT AVG(pron_overall) FROM ("
          "  SELECT pron_overall FROM pronunciation_attempts "
          "  WHERE pron_overall IS NOT NULL "
          "  ORDER BY id DESC LIMIT ?)";
    auto q = stmt_cache_->acquire(
        user_id ? "pron.recent_avg_user" : "pron.recent_avg",
        sql);
    if (!q) return std::nullopt;
    int idx = 1;
    if (user_id) sqlite3_bind_int64(q.get(), idx++, *user_id);
    sqlite3_bind_int(q.get(), idx, limit);
    if (sqlite3_step(q.get()) != SQLITE_ROW) return std::nullopt;
    if (sqlite3_column_type(q.get(), 0) == SQLITE_NULL) return std::nullopt;
    return static_cast<float>(sqlite3_column_double(q.get(), 0));
#endif
}

} // namespace hecquin::learning
