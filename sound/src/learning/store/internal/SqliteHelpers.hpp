// Private header — shared across every LearningStore*.cpp translation unit.
//
// Not part of the public API of the learning library: do not include from
// outside `learning/`. The helpers here centralise the SQLite RAII + error
// plumbing so each feature-specific LearningStore*.cpp file stays focused on
// its own SQL.
#pragma once

#ifdef HECQUIN_WITH_SQLITE

#include <sqlite3.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>

namespace hecquin::learning::detail {

/**
 * RAII owner for a prepared statement. Finalises on scope exit so every
 * error path in the repositories becomes a plain `return`.
 */
struct StmtGuard {
    sqlite3_stmt* stmt = nullptr;

    StmtGuard() = default;
    explicit StmtGuard(sqlite3_stmt* s) : stmt(s) {}
    StmtGuard(const StmtGuard&) = delete;
    StmtGuard& operator=(const StmtGuard&) = delete;
    StmtGuard(StmtGuard&& o) noexcept : stmt(o.stmt) { o.stmt = nullptr; }
    StmtGuard& operator=(StmtGuard&& o) noexcept {
        if (this != &o) {
            if (stmt) sqlite3_finalize(stmt);
            stmt = o.stmt;
            o.stmt = nullptr;
        }
        return *this;
    }
    ~StmtGuard() { if (stmt) sqlite3_finalize(stmt); }

    explicit operator bool() const { return stmt != nullptr; }
    sqlite3_stmt* get() const { return stmt; }
};

/**
 * RAII transaction helper. Rolls back on scope exit unless `commit()` is
 * called. Safe to nest only via savepoints (not used here).
 */
class Transaction {
public:
    explicit Transaction(sqlite3* db, const char* begin_sql = "BEGIN IMMEDIATE;")
        : db_(db) {
        if (db_ && sqlite3_exec(db_, begin_sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
            std::cerr << "[LearningStore] BEGIN failed: " << sqlite3_errmsg(db_) << std::endl;
            db_ = nullptr;
        }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction() {
        if (db_) sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    }

    bool active() const { return db_ != nullptr; }
    bool commit() {
        if (!db_) return false;
        const bool ok =
            sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
        db_ = nullptr;
        return ok;
    }

private:
    sqlite3* db_;
};

/** Unix epoch seconds at call time. */
inline int64_t now_epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

/** Ensure the parent directory of `path` exists, creating it if needed. */
inline bool ensure_parent_dir(const std::string& path) {
    namespace fs = std::filesystem;
    const fs::path parent = fs::path(path).parent_path();
    if (parent.empty()) return true;
    std::error_code ec;
    fs::create_directories(parent, ec);
    return !ec;
}

/**
 * Prepare a statement or log the failure and return an empty guard. Callers
 * check `bool(guard)` to branch, keeping the happy path linear.
 */
inline StmtGuard prepare_or_log(sqlite3* db, const char* sql, const char* tag) {
    sqlite3_stmt* raw = nullptr;
    const int rc = sqlite3_prepare_v2(db, sql, -1, &raw, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[LearningStore] prepare failed (" << tag << "): "
                  << sqlite3_errmsg(db) << std::endl;
        if (raw) sqlite3_finalize(raw);
        return StmtGuard{};
    }
    return StmtGuard{raw};
}

/** Step a statement that must return DONE (writes). Logs + returns false otherwise. */
inline bool step_done(sqlite3* db, sqlite3_stmt* s, const char* tag) {
    const int rc = sqlite3_step(s);
    if (rc != SQLITE_DONE) {
        std::cerr << "[LearningStore] step failed (" << tag << "): "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

/**
 * Prepared-statement cache.
 *
 * Amortises `sqlite3_prepare_v2` (which is ~10× more expensive than a step
 * for the short queries we run) across repeated calls.  The first call for
 * a given `tag` compiles + stores the statement; subsequent calls reset +
 * rebind and reuse it.  `tag` is the cache key, not `sql` itself, so callers
 * can keep human-readable tags like "topk.vec" or "api_calls.insert".
 *
 * Threading: the cache owns a mutex and hands out a `CachedStmt` RAII handle
 * that holds the mutex for its lifetime — matching the serialisation
 * guarantee SQLite's default threading mode gives on a single connection.
 * The cache must be cleared before `sqlite3_close` (statements hold refs on
 * the connection); `LearningStore::~LearningStore` does this via destruction
 * of the owning `unique_ptr`.
 */
class StatementCache;

class CachedStmt {
public:
    CachedStmt() = default;
    CachedStmt(sqlite3_stmt* s, std::unique_lock<std::mutex>&& lock)
        : stmt_(s), lock_(std::move(lock)) {}
    CachedStmt(const CachedStmt&) = delete;
    CachedStmt& operator=(const CachedStmt&) = delete;
    CachedStmt(CachedStmt&& o) noexcept : stmt_(o.stmt_), lock_(std::move(o.lock_)) {
        o.stmt_ = nullptr;
    }
    CachedStmt& operator=(CachedStmt&& o) noexcept {
        if (this != &o) {
            release_();
            stmt_ = o.stmt_;
            lock_ = std::move(o.lock_);
            o.stmt_ = nullptr;
        }
        return *this;
    }
    ~CachedStmt() { release_(); }

    explicit operator bool() const { return stmt_ != nullptr; }
    sqlite3_stmt* get() const { return stmt_; }

private:
    void release_() {
        if (stmt_) {
            // Leave the compiled statement in the cache, but drop any
            // bindings + result cursor so the next caller starts clean.
            sqlite3_reset(stmt_);
            sqlite3_clear_bindings(stmt_);
            stmt_ = nullptr;
        }
        if (lock_.owns_lock()) lock_.unlock();
    }

    sqlite3_stmt* stmt_ = nullptr;
    std::unique_lock<std::mutex> lock_;
};

class StatementCache {
public:
    explicit StatementCache(sqlite3* db) : db_(db) {}
    ~StatementCache() = default;
    StatementCache(const StatementCache&) = delete;
    StatementCache& operator=(const StatementCache&) = delete;

    /**
     * Return a ready-to-bind cached statement for `tag`.  Falls back to
     * compile-then-store on first use.  Returns an empty handle (`bool false`)
     * and logs on compile failure.
     */
    CachedStmt acquire(const char* tag, const char* sql) {
        std::unique_lock<std::mutex> lock(mu_);
        auto it = cache_.find(tag);
        if (it == cache_.end()) {
            sqlite3_stmt* raw = nullptr;
            const int rc = sqlite3_prepare_v2(db_, sql, -1, &raw, nullptr);
            if (rc != SQLITE_OK) {
                std::cerr << "[LearningStore] prepare_cached failed (" << tag
                          << "): " << sqlite3_errmsg(db_) << std::endl;
                if (raw) sqlite3_finalize(raw);
                return CachedStmt{};
            }
            it = cache_.emplace(tag, StmtGuard{raw}).first;
        }
        sqlite3_stmt* stmt = it->second.get();
        return CachedStmt{stmt, std::move(lock)};
    }

private:
    sqlite3* db_;
    std::mutex mu_;
    std::unordered_map<std::string, StmtGuard> cache_;
};

} // namespace hecquin::learning::detail

#endif // HECQUIN_WITH_SQLITE
