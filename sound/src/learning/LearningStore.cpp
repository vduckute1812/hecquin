// LearningStore.cpp — lifecycle + core metadata helpers only.
//
// Feature-specific SQL lives in sibling translation units so this file stays
// small and easy to audit:
//
//   * LearningStoreMigrations.cpp    — DDL / schema versioning
//   * LearningStoreDocuments.cpp     — documents + ingested_files + drill pool
//   * LearningStoreVectorSearch.cpp  — query_top_k (vec0 and fallback)
//   * LearningStoreSessions.cpp      — sessions + interactions + vocab
//   * LearningStorePronunciation.cpp — pronunciation_attempts + phoneme_mastery
//   * LearningStoreApiCalls.cpp      — api_calls log (written by the C++ decorator)
//
// All files share `internal/SqliteHelpers.hpp` for the RAII glue.

#include "learning/LearningStore.hpp"

#ifdef HECQUIN_WITH_SQLITE
#include "learning/internal/SqliteHelpers.hpp"
#include <sqlite3.h>
#endif

#ifdef HECQUIN_WITH_SQLITE_VEC
extern "C" {
#include "sqlite-vec.h"
}
#endif

#include <iostream>
#include <utility>

namespace hecquin::learning {

#ifdef HECQUIN_WITH_SQLITE
using detail::prepare_or_log;
using detail::step_done;
using detail::ensure_parent_dir;
#endif

LearningStore::LearningStore(std::string db_path, int embedding_dim)
    : db_path_(std::move(db_path)), embedding_dim_(embedding_dim) {}

LearningStore::~LearningStore() {
#ifdef HECQUIN_WITH_SQLITE
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
#endif
}

bool LearningStore::exec_(const char* sql) {
#ifdef HECQUIN_WITH_SQLITE
    char* err = nullptr;
    const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::cerr << "[LearningStore] SQL error: " << (err ? err : "?")
                  << "\n  SQL: " << sql << std::endl;
        if (err) sqlite3_free(err);
        return false;
    }
    return true;
#else
    (void)sql;
    return false;
#endif
}

bool LearningStore::open() {
#ifndef HECQUIN_WITH_SQLITE
    std::cerr << "[LearningStore] Built without SQLite support; learning features disabled."
              << std::endl;
    return false;
#else
    ensure_parent_dir(db_path_);

    if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "[LearningStore] cannot open DB at " << db_path_ << ": "
                  << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

#ifdef HECQUIN_WITH_SQLITE_VEC
    char* vec_err = nullptr;
    const int vrc = sqlite3_vec_init(db_, &vec_err, nullptr);
    if (vrc != SQLITE_OK) {
        std::cerr << "[LearningStore] sqlite-vec init failed: "
                  << (vec_err ? vec_err : "?") << std::endl;
        if (vec_err) sqlite3_free(vec_err);
        has_vec0_ = false;
    } else {
        has_vec0_ = true;
    }
#endif

    if (!run_migrations_()) return false;
    if (!check_embedding_dim_()) return false;
    return true;
#endif
}

bool LearningStore::check_embedding_dim_() {
#ifndef HECQUIN_WITH_SQLITE
    return false;
#else
    const auto stored = get_meta_("embedding_dim");
    if (!stored) {
        return set_meta_("embedding_dim", std::to_string(embedding_dim_));
    }
    int stored_dim = 0;
    try {
        stored_dim = std::stoi(*stored);
    } catch (...) {
        stored_dim = 0;
    }
    if (stored_dim != embedding_dim_) {
        std::cerr << "[LearningStore] REFUSING TO OPEN: embedding dim mismatch. "
                  << "DB was built with dim=" << stored_dim
                  << " but config requests dim=" << embedding_dim_ << ". "
                  << "Rebuild the DB (delete " << db_path_ << ") or keep the original dim."
                  << std::endl;
        return false;
    }
    return true;
#endif
}

bool LearningStore::set_meta_(const char* key, const std::string& value) {
#ifndef HECQUIN_WITH_SQLITE
    (void)key; (void)value;
    return false;
#else
    if (!db_) return false;
    auto stmt = prepare_or_log(db_,
        "INSERT INTO kv_metadata (key, value) VALUES (?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value;",
        "set_meta");
    if (!stmt) return false;
    sqlite3_bind_text(stmt.get(), 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt.get(), 2, value.c_str(), -1, SQLITE_TRANSIENT);
    return step_done(db_, stmt.get(), "set_meta");
#endif
}

std::optional<std::string> LearningStore::get_meta_(const char* key) const {
#ifndef HECQUIN_WITH_SQLITE
    (void)key;
    return std::nullopt;
#else
    if (!db_) return std::nullopt;
    auto stmt = prepare_or_log(db_, "SELECT value FROM kv_metadata WHERE key = ?;", "get_meta");
    if (!stmt) return std::nullopt;
    sqlite3_bind_text(stmt.get(), 1, key, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const unsigned char* v = sqlite3_column_text(stmt.get(), 0);
        return v ? std::optional<std::string>(reinterpret_cast<const char*>(v))
                 : std::optional<std::string>(std::string{});
    }
    return std::nullopt;
#endif
}

} // namespace hecquin::learning
