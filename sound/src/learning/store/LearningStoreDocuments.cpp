// Document + ingestion + drill-sample operations.
//
// Groups everything that writes to or reads from the `documents`,
// `vec_documents`, and `ingested_files` tables.  The TU now owns the
// free-function implementations in `hecquin::learning::store::detail`;
// the `LearningStore::…` methods at the bottom are one-line forwarders
// (Option A of the store refactor).

#include "learning/store/LearningStore.hpp"

#ifdef HECQUIN_WITH_SQLITE
#include "learning/store/detail/DocumentsOps.hpp"
#include "learning/store/internal/SqliteHelpers.hpp"
#include <sqlite3.h>
#include <iostream>
#endif

namespace hecquin::learning {

#ifdef HECQUIN_WITH_SQLITE

namespace store::detail {

std::optional<int64_t>
upsert_document(sqlite3* db,
                learning::detail::StatementCache& /*cache*/,
                int embedding_dim,
                const DocumentRecord& doc,
                const std::vector<float>& embedding) {
    using learning::detail::prepare_or_log;
    using learning::detail::step_done;
    using learning::detail::Transaction;
    using learning::detail::now_epoch_seconds;

    if (!db) return std::nullopt;
    if (static_cast<int>(embedding.size()) != embedding_dim) {
        std::cerr << "[LearningStore] embedding dim mismatch: got " << embedding.size()
                  << " expected " << embedding_dim << std::endl;
        return std::nullopt;
    }

    Transaction tx(db);
    if (!tx.active()) return std::nullopt;

    {
        auto del = prepare_or_log(db,
            "DELETE FROM documents WHERE source = ? AND kind = ? AND title = ?;",
            "upsert.del");
        if (!del) return std::nullopt;
        sqlite3_bind_text(del.get(), 1, doc.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(del.get(), 2, doc.kind.c_str(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(del.get(), 3, doc.title.c_str(),  -1, SQLITE_TRANSIENT);
        if (!step_done(db, del.get(), "upsert.del")) return std::nullopt;
    }

    int64_t rowid = 0;
    {
        auto ins = prepare_or_log(db,
            "INSERT INTO documents (source, kind, title, body, metadata_json, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?);",
            "upsert.ins");
        if (!ins) return std::nullopt;
        sqlite3_bind_text(ins.get(), 1, doc.source.c_str(),        -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins.get(), 2, doc.kind.c_str(),          -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins.get(), 3, doc.title.c_str(),         -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins.get(), 4, doc.body.c_str(),          -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins.get(), 5, doc.metadata_json.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(ins.get(), 6, now_epoch_seconds());
        if (!step_done(db, ins.get(), "upsert.ins")) return std::nullopt;
        rowid = sqlite3_last_insert_rowid(db);
    }

    {
        auto vdel = prepare_or_log(db,
            "DELETE FROM vec_documents WHERE rowid = ?;", "upsert.vdel");
        if (!vdel) return std::nullopt;
        sqlite3_bind_int64(vdel.get(), 1, rowid);
        if (!step_done(db, vdel.get(), "upsert.vdel")) return std::nullopt;
    }

    {
        auto vec_ins = prepare_or_log(db,
            "INSERT INTO vec_documents (rowid, embedding) VALUES (?, ?);",
            "upsert.vins");
        if (!vec_ins) return std::nullopt;
        sqlite3_bind_int64(vec_ins.get(), 1, rowid);
        sqlite3_bind_blob(vec_ins.get(), 2, embedding.data(),
                          static_cast<int>(embedding.size() * sizeof(float)),
                          SQLITE_TRANSIENT);
        if (!step_done(db, vec_ins.get(), "upsert.vins")) return std::nullopt;
    }

    if (!tx.commit()) return std::nullopt;
    return rowid;
}

bool is_file_already_ingested(sqlite3* db,
                              const std::string& path,
                              const std::string& hash) {
    using learning::detail::prepare_or_log;
    if (!db) return false;
    auto q = prepare_or_log(db,
        "SELECT hash FROM ingested_files WHERE path = ?;", "ingested.lookup");
    if (!q) return false;
    sqlite3_bind_text(q.get(), 1, path.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(q.get()) == SQLITE_ROW) {
        const unsigned char* existing = sqlite3_column_text(q.get(), 0);
        return existing && hash == reinterpret_cast<const char*>(existing);
    }
    return false;
}

void record_ingested_file(sqlite3* db,
                          const std::string& path,
                          const std::string& hash) {
    using learning::detail::prepare_or_log;
    using learning::detail::step_done;
    using learning::detail::now_epoch_seconds;
    if (!db) return;
    auto q = prepare_or_log(db,
        "INSERT INTO ingested_files (path, hash, ingested_at) VALUES (?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET hash = excluded.hash, "
        "ingested_at = excluded.ingested_at;",
        "ingested.record");
    if (!q) return;
    sqlite3_bind_text(q.get(), 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.get(), 2, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.get(), 3, now_epoch_seconds());
    step_done(db, q.get(), "ingested.record");
}

std::vector<std::string> sample_drill_sentences(sqlite3* db, int limit) {
    using learning::detail::prepare_or_log;
    std::vector<std::string> out;
    if (!db || limit <= 0) return out;
    auto q = prepare_or_log(db,
        "SELECT body FROM documents WHERE kind = 'drill' "
        "ORDER BY RANDOM() LIMIT ?;",
        "drill.sample");
    if (!q) return out;
    sqlite3_bind_int(q.get(), 1, limit);
    while (sqlite3_step(q.get()) == SQLITE_ROW) {
        const unsigned char* t = sqlite3_column_text(q.get(), 0);
        if (t) out.emplace_back(reinterpret_cast<const char*>(t));
    }
    return out;
}

} // namespace store::detail

#endif // HECQUIN_WITH_SQLITE

// ---------------------------------------------------------------------
// LearningStore façade: every method below is a one-line forwarder to
// the free function in `store::detail`.  Keep this section thin.
// ---------------------------------------------------------------------

std::optional<int64_t> LearningStore::upsert_document(const DocumentRecord& doc,
                                                      const std::vector<float>& embedding) {
#ifndef HECQUIN_WITH_SQLITE
    (void)doc; (void)embedding;
    return std::nullopt;
#else
    return store::detail::upsert_document(db_, *stmt_cache_, embedding_dim_, doc, embedding);
#endif
}

bool LearningStore::is_file_already_ingested(const std::string& path,
                                             const std::string& hash) const {
#ifndef HECQUIN_WITH_SQLITE
    (void)path; (void)hash;
    return false;
#else
    return store::detail::is_file_already_ingested(db_, path, hash);
#endif
}

void LearningStore::record_ingested_file(const std::string& path, const std::string& hash) {
#ifndef HECQUIN_WITH_SQLITE
    (void)path; (void)hash;
#else
    store::detail::record_ingested_file(db_, path, hash);
#endif
}

std::vector<std::string> LearningStore::sample_drill_sentences(int limit) const {
#ifndef HECQUIN_WITH_SQLITE
    (void)limit;
    return {};
#else
    return store::detail::sample_drill_sentences(db_, limit);
#endif
}

} // namespace hecquin::learning
