#include "learning/LearningStore.hpp"

#ifdef HECQUIN_WITH_SQLITE
#include <sqlite3.h>
#endif

#ifdef HECQUIN_WITH_SQLITE_VEC
extern "C" {
#include "sqlite-vec.h"
}
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <queue>
#include <sstream>
#include <filesystem>
#include <system_error>

namespace hecquin::learning {

namespace {

#ifdef HECQUIN_WITH_SQLITE

/**
 * RAII owner for a prepared statement.  Transferring construction via
 * `make_stmt()` below makes the "prepare may fail" branch a single `return`.
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
 * RAII transaction helper.  Rolls back on scope exit unless `commit()` is
 * called.  Safe to nest only via savepoints (not used here).
 */
class Transaction {
public:
    Transaction(sqlite3* db, const char* begin_sql = "BEGIN IMMEDIATE;")
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

int64_t now_epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool ensure_parent_dir(const std::string& path) {
    namespace fs = std::filesystem;
    const fs::path parent = fs::path(path).parent_path();
    if (parent.empty()) return true;
    std::error_code ec;
    fs::create_directories(parent, ec);
    return !ec;
}

/** Prepare or log + return an empty guard. */
StmtGuard prepare_or_log(sqlite3* db, const char* sql, const char* tag) {
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

bool step_done(sqlite3* db, sqlite3_stmt* s, const char* tag) {
    const int rc = sqlite3_step(s);
    if (rc != SQLITE_DONE) {
        std::cerr << "[LearningStore] step failed (" << tag << "): "
                  << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

#endif  // HECQUIN_WITH_SQLITE

} // namespace

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

bool LearningStore::run_migrations_() {
#ifndef HECQUIN_WITH_SQLITE
    return false;
#else
    // kv_metadata holds schema_version, embedding_dim, and any future knobs.
    if (!exec_("CREATE TABLE IF NOT EXISTS kv_metadata ("
               "  key TEXT PRIMARY KEY,"
               "  value TEXT"
               ");")) return false;

    if (!exec_("CREATE TABLE IF NOT EXISTS documents ("
               "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "  source TEXT NOT NULL,"
               "  kind TEXT NOT NULL,"
               "  title TEXT NOT NULL,"
               "  body TEXT NOT NULL,"
               "  metadata_json TEXT DEFAULT '{}',"
               "  created_at INTEGER NOT NULL"
               ");")) return false;

    if (!exec_("CREATE INDEX IF NOT EXISTS idx_documents_kind ON documents(kind);"))
        return false;
    if (!exec_("CREATE UNIQUE INDEX IF NOT EXISTS idx_documents_identity "
               "ON documents(source, kind, title);"))
        return false;

    if (!exec_("CREATE TABLE IF NOT EXISTS ingested_files ("
               "  path TEXT PRIMARY KEY,"
               "  hash TEXT NOT NULL,"
               "  ingested_at INTEGER NOT NULL"
               ");")) return false;

    if (!exec_("CREATE TABLE IF NOT EXISTS sessions ("
               "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "  mode TEXT NOT NULL,"
               "  started_at INTEGER NOT NULL,"
               "  ended_at INTEGER"
               ");")) return false;

    if (!exec_("CREATE TABLE IF NOT EXISTS interactions ("
               "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "  session_id INTEGER REFERENCES sessions(id),"
               "  user_text TEXT NOT NULL,"
               "  corrected_text TEXT,"
               "  grammar_notes TEXT,"
               "  created_at INTEGER NOT NULL"
               ");")) return false;

    if (!exec_("CREATE TABLE IF NOT EXISTS vocab_progress ("
               "  word TEXT PRIMARY KEY,"
               "  first_seen_at INTEGER NOT NULL,"
               "  last_seen_at INTEGER NOT NULL,"
               "  seen_count INTEGER NOT NULL DEFAULT 1,"
               "  mastery INTEGER NOT NULL DEFAULT 0"
               ");")) return false;

    if (!exec_("CREATE TABLE IF NOT EXISTS pronunciation_attempts ("
               "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "  session_id INTEGER REFERENCES sessions(id),"
               "  reference TEXT NOT NULL,"
               "  transcript TEXT,"
               "  pron_overall REAL,"
               "  intonation_overall REAL,"
               "  per_phoneme_json TEXT,"
               "  created_at INTEGER NOT NULL"
               ");")) return false;

    if (!exec_("CREATE TABLE IF NOT EXISTS phoneme_mastery ("
               "  ipa TEXT PRIMARY KEY,"
               "  attempts INTEGER NOT NULL DEFAULT 0,"
               "  avg_score REAL NOT NULL DEFAULT 0.0,"
               "  last_seen_at INTEGER NOT NULL"
               ");")) return false;

    if (has_vec0_) {
        std::ostringstream sql;
        sql << "CREATE VIRTUAL TABLE IF NOT EXISTS vec_documents USING vec0("
            << "embedding FLOAT[" << embedding_dim_ << "]);";
        if (!exec_(sql.str().c_str())) return false;
    } else {
        if (!exec_("CREATE TABLE IF NOT EXISTS vec_documents ("
                   "  rowid INTEGER PRIMARY KEY,"
                   "  embedding BLOB NOT NULL"
                   ");")) return false;
    }

    // Stamp schema_version if not yet present.
    if (!get_meta_("schema_version")) {
        if (!set_meta_("schema_version", std::to_string(kSchemaVersion))) return false;
    }

    return true;
#endif
}

bool LearningStore::check_embedding_dim_() {
#ifndef HECQUIN_WITH_SQLITE
    return false;
#else
    const auto stored = get_meta_("embedding_dim");
    if (!stored) {
        // First open — record the dim the caller asked for.
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

std::vector<RetrievedDocument>
LearningStore::query_top_k(const std::vector<float>& embedding, int k) const {
    std::vector<RetrievedDocument> out;
#ifndef HECQUIN_WITH_SQLITE
    (void)embedding; (void)k;
    return out;
#else
    if (!db_ || k <= 0) return out;
    if (static_cast<int>(embedding.size()) != embedding_dim_) {
        std::cerr << "[LearningStore] query dim mismatch: got " << embedding.size()
                  << " expected " << embedding_dim_ << std::endl;
        return out;
    }

    if (has_vec0_) {
        auto q = prepare_or_log(db_,
            "SELECT d.id, d.source, d.kind, d.title, d.body, d.metadata_json, v.distance "
            "FROM vec_documents v JOIN documents d ON d.id = v.rowid "
            "WHERE v.embedding MATCH ? AND k = ? ORDER BY v.distance;",
            "topk.vec");
        if (!q) return out;
        sqlite3_bind_blob(q.get(), 1, embedding.data(),
                          static_cast<int>(embedding.size() * sizeof(float)),
                          SQLITE_TRANSIENT);
        sqlite3_bind_int(q.get(), 2, k);
        while (sqlite3_step(q.get()) == SQLITE_ROW) {
            RetrievedDocument r;
            r.doc.id = sqlite3_column_int64(q.get(), 0);
            auto str = [&](int col) {
                const unsigned char* t = sqlite3_column_text(q.get(), col);
                return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
            };
            r.doc.source = str(1);
            r.doc.kind = str(2);
            r.doc.title = str(3);
            r.doc.body = str(4);
            r.doc.metadata_json = str(5);
            r.distance = static_cast<float>(sqlite3_column_double(q.get(), 6));
            out.push_back(std::move(r));
        }
        return out;
    }

    auto scan = prepare_or_log(db_,
        "SELECT d.id, d.source, d.kind, d.title, d.body, d.metadata_json, v.embedding "
        "FROM vec_documents v JOIN documents d ON d.id = v.rowid;",
        "topk.scan");
    if (!scan) return out;

    auto norm = [](const float* v, int n) {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += static_cast<double>(v[i]) * v[i];
        return std::sqrt(s);
    };
    const double q_norm = norm(embedding.data(), static_cast<int>(embedding.size()));
    if (q_norm < 1e-9) return out;

    using Scored = std::pair<float, RetrievedDocument>;
    auto cmp = [](const Scored& a, const Scored& b) { return a.first < b.first; };
    std::priority_queue<Scored, std::vector<Scored>, decltype(cmp)> heap(cmp);

    while (sqlite3_step(scan.get()) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(scan.get(), 6);
        const int bytes = sqlite3_column_bytes(scan.get(), 6);
        const int n = bytes / static_cast<int>(sizeof(float));
        if (n != static_cast<int>(embedding.size())) continue;
        const float* vec = static_cast<const float*>(blob);
        double dot = 0.0;
        for (int i = 0; i < n; ++i) dot += static_cast<double>(embedding[i]) * vec[i];
        const double vn = norm(vec, n);
        if (vn < 1e-9) continue;
        const float distance = static_cast<float>(1.0 - dot / (q_norm * vn));

        RetrievedDocument r;
        r.doc.id = sqlite3_column_int64(scan.get(), 0);
        auto str = [&](int col) {
            const unsigned char* t = sqlite3_column_text(scan.get(), col);
            return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
        };
        r.doc.source = str(1);
        r.doc.kind = str(2);
        r.doc.title = str(3);
        r.doc.body = str(4);
        r.doc.metadata_json = str(5);
        r.distance = distance;

        if (static_cast<int>(heap.size()) < k) {
            heap.emplace(distance, std::move(r));
        } else if (distance < heap.top().first) {
            heap.pop();
            heap.emplace(distance, std::move(r));
        }
    }

    while (!heap.empty()) {
        out.push_back(heap.top().second);
        heap.pop();
    }
    std::reverse(out.begin(), out.end());
    return out;
#endif
}

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
        if (!q) return;  // dtor rolls back
        sqlite3_bind_text(q.get(),  1, w.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(q.get(), 2, now);
        sqlite3_bind_int64(q.get(), 3, now);
        if (!step_done(db_, q.get(), "vocab.touch")) return;  // dtor rolls back
    }

    tx.commit();
#endif
}

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
