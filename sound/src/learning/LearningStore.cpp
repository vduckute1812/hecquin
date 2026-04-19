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
#include <sys/stat.h>

namespace hecquin::learning {

namespace {

#ifdef HECQUIN_WITH_SQLITE
struct StmtGuard {
    sqlite3_stmt* stmt = nullptr;
    ~StmtGuard() { if (stmt) sqlite3_finalize(stmt); }
};

int64_t now_epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool ensure_parent_dir(const std::string& path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos) return true;
    std::string dir;
    for (size_t i = 0; i < slash; ++i) {
        dir.push_back(path[i]);
        if (path[i] == '/' && !dir.empty()) {
            mkdir(dir.c_str(), 0755);
        }
    }
    if (!dir.empty()) mkdir(dir.c_str(), 0755);
    return true;
}
#endif

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
        std::cerr << "[LearningStore] SQL error: " << (err ? err : "?") << "\n  SQL: " << sql << std::endl;
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
    std::cerr << "[LearningStore] Built without SQLite support; learning features disabled." << std::endl;
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
    // sqlite-vec auto-registers when linked statically via its init entry point.
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

    return run_migrations_();
#endif
}

bool LearningStore::run_migrations_() {
#ifndef HECQUIN_WITH_SQLITE
    return false;
#else
    if (!exec_("CREATE TABLE IF NOT EXISTS documents ("
               "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "  source TEXT NOT NULL,"
               "  kind TEXT NOT NULL,"
               "  title TEXT NOT NULL,"
               "  body TEXT NOT NULL,"
               "  metadata_json TEXT DEFAULT '{}',"
               "  created_at INTEGER NOT NULL"
               ");")) return false;

    if (!exec_("CREATE INDEX IF NOT EXISTS idx_documents_kind ON documents(kind);")) return false;

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

    // Embedding storage: either sqlite-vec virtual table or a plain BLOB fallback.
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

    return true;
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

    StmtGuard del;
    sqlite3_prepare_v2(db_,
        "DELETE FROM documents WHERE source = ? AND kind = ? AND title = ?;",
        -1, &del.stmt, nullptr);
    sqlite3_bind_text(del.stmt, 1, doc.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(del.stmt, 2, doc.kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(del.stmt, 3, doc.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(del.stmt);

    StmtGuard ins;
    if (sqlite3_prepare_v2(db_,
        "INSERT INTO documents (source, kind, title, body, metadata_json, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?);",
        -1, &ins.stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[LearningStore] insert prepare failed: " << sqlite3_errmsg(db_) << std::endl;
        return std::nullopt;
    }
    sqlite3_bind_text(ins.stmt, 1, doc.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins.stmt, 2, doc.kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins.stmt, 3, doc.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins.stmt, 4, doc.body.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins.stmt, 5, doc.metadata_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(ins.stmt, 6, now_epoch_seconds());
    if (sqlite3_step(ins.stmt) != SQLITE_DONE) {
        std::cerr << "[LearningStore] insert failed: " << sqlite3_errmsg(db_) << std::endl;
        return std::nullopt;
    }
    const int64_t rowid = sqlite3_last_insert_rowid(db_);

    // Remove any previous embedding for this rowid, then insert fresh.
    {
        StmtGuard vdel;
        sqlite3_prepare_v2(db_, "DELETE FROM vec_documents WHERE rowid = ?;",
                           -1, &vdel.stmt, nullptr);
        sqlite3_bind_int64(vdel.stmt, 1, rowid);
        sqlite3_step(vdel.stmt);
    }

    StmtGuard vec_ins;
    if (sqlite3_prepare_v2(db_,
        "INSERT INTO vec_documents (rowid, embedding) VALUES (?, ?);",
        -1, &vec_ins.stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[LearningStore] vec insert prepare failed: " << sqlite3_errmsg(db_) << std::endl;
        return std::nullopt;
    }
    sqlite3_bind_int64(vec_ins.stmt, 1, rowid);
    sqlite3_bind_blob(vec_ins.stmt, 2, embedding.data(),
                      static_cast<int>(embedding.size() * sizeof(float)), SQLITE_TRANSIENT);
    if (sqlite3_step(vec_ins.stmt) != SQLITE_DONE) {
        std::cerr << "[LearningStore] vec insert failed: " << sqlite3_errmsg(db_) << std::endl;
        return std::nullopt;
    }
    return rowid;
#endif
}

bool LearningStore::is_file_already_ingested(const std::string& path, const std::string& hash) const {
#ifndef HECQUIN_WITH_SQLITE
    (void)path; (void)hash;
    return false;
#else
    if (!db_) return false;
    StmtGuard q;
    sqlite3_prepare_v2(db_, "SELECT hash FROM ingested_files WHERE path = ?;",
                       -1, &q.stmt, nullptr);
    sqlite3_bind_text(q.stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(q.stmt) == SQLITE_ROW) {
        const unsigned char* existing = sqlite3_column_text(q.stmt, 0);
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
    StmtGuard q;
    sqlite3_prepare_v2(db_,
        "INSERT INTO ingested_files (path, hash, ingested_at) VALUES (?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET hash = excluded.hash, ingested_at = excluded.ingested_at;",
        -1, &q.stmt, nullptr);
    sqlite3_bind_text(q.stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.stmt, 3, now_epoch_seconds());
    sqlite3_step(q.stmt);
#endif
}

std::vector<RetrievedDocument> LearningStore::query_top_k(const std::vector<float>& embedding, int k) const {
    std::vector<RetrievedDocument> out;
#ifndef HECQUIN_WITH_SQLITE
    (void)embedding; (void)k;
    return out;
#else
    if (!db_ || k <= 0) return out;

    if (has_vec0_) {
        StmtGuard q;
        const char* sql =
            "SELECT d.id, d.source, d.kind, d.title, d.body, d.metadata_json, v.distance "
            "FROM vec_documents v JOIN documents d ON d.id = v.rowid "
            "WHERE v.embedding MATCH ? AND k = ? ORDER BY v.distance;";
        if (sqlite3_prepare_v2(db_, sql, -1, &q.stmt, nullptr) != SQLITE_OK) {
            std::cerr << "[LearningStore] vec query prepare failed: " << sqlite3_errmsg(db_) << std::endl;
            return out;
        }
        sqlite3_bind_blob(q.stmt, 1, embedding.data(),
                          static_cast<int>(embedding.size() * sizeof(float)), SQLITE_TRANSIENT);
        sqlite3_bind_int(q.stmt, 2, k);
        while (sqlite3_step(q.stmt) == SQLITE_ROW) {
            RetrievedDocument r;
            r.doc.id = sqlite3_column_int64(q.stmt, 0);
            auto str = [&](int col) {
                const unsigned char* t = sqlite3_column_text(q.stmt, col);
                return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
            };
            r.doc.source = str(1);
            r.doc.kind = str(2);
            r.doc.title = str(3);
            r.doc.body = str(4);
            r.doc.metadata_json = str(5);
            r.distance = static_cast<float>(sqlite3_column_double(q.stmt, 6));
            out.push_back(std::move(r));
        }
        return out;
    }

    // Fallback: brute-force cosine scan.
    StmtGuard scan;
    const char* sql =
        "SELECT d.id, d.source, d.kind, d.title, d.body, d.metadata_json, v.embedding "
        "FROM vec_documents v JOIN documents d ON d.id = v.rowid;";
    if (sqlite3_prepare_v2(db_, sql, -1, &scan.stmt, nullptr) != SQLITE_OK) return out;

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

    while (sqlite3_step(scan.stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(scan.stmt, 6);
        const int bytes = sqlite3_column_bytes(scan.stmt, 6);
        const int n = bytes / static_cast<int>(sizeof(float));
        if (n != static_cast<int>(embedding.size())) continue;
        const float* vec = static_cast<const float*>(blob);
        double dot = 0.0;
        for (int i = 0; i < n; ++i) dot += static_cast<double>(embedding[i]) * vec[i];
        const double vn = norm(vec, n);
        if (vn < 1e-9) continue;
        const float distance = static_cast<float>(1.0 - dot / (q_norm * vn));

        RetrievedDocument r;
        r.doc.id = sqlite3_column_int64(scan.stmt, 0);
        auto str = [&](int col) {
            const unsigned char* t = sqlite3_column_text(scan.stmt, col);
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
    StmtGuard q;
    sqlite3_prepare_v2(db_,
        "INSERT INTO sessions (mode, started_at) VALUES (?, ?);",
        -1, &q.stmt, nullptr);
    sqlite3_bind_text(q.stmt, 1, mode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.stmt, 2, now_epoch_seconds());
    sqlite3_step(q.stmt);
    return sqlite3_last_insert_rowid(db_);
#endif
}

void LearningStore::end_session(int64_t session_id) {
#ifndef HECQUIN_WITH_SQLITE
    (void)session_id;
#else
    if (!db_ || session_id <= 0) return;
    StmtGuard q;
    sqlite3_prepare_v2(db_,
        "UPDATE sessions SET ended_at = ? WHERE id = ?;",
        -1, &q.stmt, nullptr);
    sqlite3_bind_int64(q.stmt, 1, now_epoch_seconds());
    sqlite3_bind_int64(q.stmt, 2, session_id);
    sqlite3_step(q.stmt);
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
    StmtGuard q;
    sqlite3_prepare_v2(db_,
        "INSERT INTO interactions (session_id, user_text, corrected_text, grammar_notes, created_at) "
        "VALUES (?, ?, ?, ?, ?);",
        -1, &q.stmt, nullptr);
    if (session_id > 0) {
        sqlite3_bind_int64(q.stmt, 1, session_id);
    } else {
        sqlite3_bind_null(q.stmt, 1);
    }
    sqlite3_bind_text(q.stmt, 2, user_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.stmt, 3, corrected_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.stmt, 4, grammar_notes.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.stmt, 5, now_epoch_seconds());
    sqlite3_step(q.stmt);
#endif
}

void LearningStore::touch_vocab(const std::vector<std::string>& words) {
#ifndef HECQUIN_WITH_SQLITE
    (void)words;
#else
    if (!db_ || words.empty()) return;
    const int64_t now = now_epoch_seconds();
    sqlite3_exec(db_, "BEGIN;", nullptr, nullptr, nullptr);
    for (const auto& raw : words) {
        std::string w;
        w.reserve(raw.size());
        for (unsigned char c : raw) {
            if (std::isalpha(c) || c == '\'') w.push_back(static_cast<char>(std::tolower(c)));
        }
        if (w.size() < 2) continue;
        StmtGuard q;
        sqlite3_prepare_v2(db_,
            "INSERT INTO vocab_progress (word, first_seen_at, last_seen_at, seen_count) "
            "VALUES (?, ?, ?, 1) "
            "ON CONFLICT(word) DO UPDATE SET last_seen_at = excluded.last_seen_at, "
            "seen_count = seen_count + 1;",
            -1, &q.stmt, nullptr);
        sqlite3_bind_text(q.stmt, 1, w.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(q.stmt, 2, now);
        sqlite3_bind_int64(q.stmt, 3, now);
        sqlite3_step(q.stmt);
    }
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
#endif
}

} // namespace hecquin::learning
