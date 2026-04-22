// Pronunciation drill + phoneme mastery operations.
//
// `pronunciation_attempts` is append-only (one row per drill); scores and
// per-phoneme JSON are stored opaquely so the schema can outlive tweaks to
// the scoring code. `phoneme_mastery` is a rolling average keyed by IPA.

#include "learning/LearningStore.hpp"

#ifdef HECQUIN_WITH_SQLITE
#include "learning/internal/SqliteHelpers.hpp"
#include <sqlite3.h>
#endif

namespace hecquin::learning {

#ifdef HECQUIN_WITH_SQLITE
using detail::prepare_or_log;
using detail::step_done;
using detail::Transaction;
using detail::now_epoch_seconds;
#endif

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
    if (!db_) return;
    auto q = prepare_or_log(db_,
        "INSERT INTO pronunciation_attempts (session_id, reference, transcript, "
        "  pron_overall, intonation_overall, per_phoneme_json, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);",
        "pron.record");
    if (!q) return;
    if (session_id > 0) {
        sqlite3_bind_int64(q.get(), 1, session_id);
    } else {
        sqlite3_bind_null(q.get(), 1);
    }
    sqlite3_bind_text(q.get(), 2, reference.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.get(), 3, transcript.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(q.get(), 4, static_cast<double>(pron_overall_0_100));
    sqlite3_bind_double(q.get(), 5, static_cast<double>(intonation_overall_0_100));
    sqlite3_bind_text(q.get(), 6, per_phoneme_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.get(), 7, now_epoch_seconds());
    step_done(db_, q.get(), "pron.record");
#endif
}

void LearningStore::touch_phoneme_mastery(
    const std::vector<std::pair<std::string, float>>& scored) {
#ifndef HECQUIN_WITH_SQLITE
    (void)scored;
#else
    if (!db_ || scored.empty()) return;
    const int64_t now = now_epoch_seconds();

    Transaction tx(db_);
    if (!tx.active()) return;

    for (const auto& [ipa, score] : scored) {
        if (ipa.empty()) continue;
        auto q = prepare_or_log(db_,
            "INSERT INTO phoneme_mastery (ipa, attempts, avg_score, last_seen_at) "
            "VALUES (?, 1, ?, ?) "
            "ON CONFLICT(ipa) DO UPDATE SET "
            "  avg_score = (avg_score * attempts + excluded.avg_score) / (attempts + 1), "
            "  attempts = attempts + 1, "
            "  last_seen_at = excluded.last_seen_at;",
            "phoneme.touch");
        if (!q) return;
        sqlite3_bind_text(q.get(), 1, ipa.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(q.get(), 2, static_cast<double>(score));
        sqlite3_bind_int64(q.get(), 3, now);
        if (!step_done(db_, q.get(), "phoneme.touch")) return;
    }
    tx.commit();
#endif
}

} // namespace hecquin::learning
