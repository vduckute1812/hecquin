// Document + ingestion + drill-sample operations.
//
// Groups everything that writes to or reads from the `documents`,
// `vec_documents`, and `ingested_files` tables — the ingestion and retrieval
// surface of the learning library.

#include "learning/store/LearningStore.hpp"

#ifdef HECQUIN_WITH_SQLITE
#include "learning/store/internal/SqliteHelpers.hpp"
#include <sqlite3.h>
#include <iostream>
#endif

namespace hecquin::learning {

#ifdef HECQUIN_WITH_SQLITE
using detail::prepare_or_log;
using detail::step_done;
using detail::Transaction;
using detail::now_epoch_seconds;
#endif

std::optional<int64_t> LearningStore::upsert_document(const DocumentRecord& doc,
                                                      const std::vector<float>& embedding) {
#ifndef HECQUIN_WITH_SQLITE
    (void)doc; (void)embedding;
    return std::nullopt;
#else
    if (!db_) return std::nullopt;
    if (static_cast<int>(embedding.size()) != embedding_dim_) {
        std::cerr << "[LearningStore] embedding dim mismatch: got " << embedding.size()
                  << " expected " << embedding_dim_ << std::endl;
        return std::nullopt;
    }

    Transaction tx(db_);
    if (!tx.active()) return std::nullopt;

    {
        auto del = prepare_or_log(db_,
            "DELETE FROM documents WHERE source = ? AND kind = ? AND title = ?;",
            "upsert.del");
        if (!del) return std::nullopt;
        sqlite3_bind_text(del.get(), 1, doc.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(del.get(), 2, doc.kind.c_str(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(del.get(), 3, doc.title.c_str(),  -1, SQLITE_TRANSIENT);
        if (!step_done(db_, del.get(), "upsert.del")) return std::nullopt;
    }

    int64_t rowid = 0;
    {
        auto ins = prepare_or_log(db_,
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
        if (!step_done(db_, ins.get(), "upsert.ins")) return std::nullopt;
        rowid = sqlite3_last_insert_rowid(db_);
    }

    {
        auto vdel = prepare_or_log(db_,
            "DELETE FROM vec_documents WHERE rowid = ?;", "upsert.vdel");
        if (!vdel) return std::nullopt;
        sqlite3_bind_int64(vdel.get(), 1, rowid);
        if (!step_done(db_, vdel.get(), "upsert.vdel")) return std::nullopt;
    }

    {
        auto vec_ins = prepare_or_log(db_,
            "INSERT INTO vec_documents (rowid, embedding) VALUES (?, ?);",
            "upsert.vins");
        if (!vec_ins) return std::nullopt;
        sqlite3_bind_int64(vec_ins.get(), 1, rowid);
        sqlite3_bind_blob(vec_ins.get(), 2, embedding.data(),
                          static_cast<int>(embedding.size() * sizeof(float)),
                          SQLITE_TRANSIENT);
        if (!step_done(db_, vec_ins.get(), "upsert.vins")) return std::nullopt;
    }

    if (!tx.commit()) return std::nullopt;
    return rowid;
#endif
}

bool LearningStore::is_file_already_ingested(const std::string& path,
                                             const std::string& hash) const {
#ifndef HECQUIN_WITH_SQLITE
    (void)path; (void)hash;
    return false;
#else
    if (!db_) return false;
    auto q = prepare_or_log(db_,
        "SELECT hash FROM ingested_files WHERE path = ?;", "ingested.lookup");
    if (!q) return false;
    sqlite3_bind_text(q.get(), 1, path.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(q.get()) == SQLITE_ROW) {
        const unsigned char* existing = sqlite3_column_text(q.get(), 0);
        return existing && hash == reinterpret_cast<const char*>(existing);
    }
    return false;
#endif
}

void LearningStore::record_ingested_file(const std::string& path, const std::string& hash) {
#ifndef HECQUIN_WITH_SQLITE
    (void)path; (void)hash;
#else
    if (!db_) return;
    auto q = prepare_or_log(db_,
        "INSERT INTO ingested_files (path, hash, ingested_at) VALUES (?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET hash = excluded.hash, "
        "ingested_at = excluded.ingested_at;",
        "ingested.record");
    if (!q) return;
    sqlite3_bind_text(q.get(), 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.get(), 2, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.get(), 3, now_epoch_seconds());
    step_done(db_, q.get(), "ingested.record");
#endif
}

std::vector<std::string> LearningStore::sample_drill_sentences(int limit) const {
    std::vector<std::string> out;
#ifndef HECQUIN_WITH_SQLITE
    (void)limit;
    return out;
#else
    if (!db_ || limit <= 0) return out;
    auto q = prepare_or_log(db_,
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
#endif
}

} // namespace hecquin::learning
