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
#include <string>
#include <system_error>

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

} // namespace hecquin::learning::detail

#endif // HECQUIN_WITH_SQLITE
