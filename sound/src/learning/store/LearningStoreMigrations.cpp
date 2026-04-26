// Schema DDL for LearningStore.
//
// All migrations must be idempotent: each CREATE uses IF NOT EXISTS so that
// `run_migrations_()` can be called on every `open()` and new tables added to
// `kSchemaVersion` simply appear on the next launch.

#include "learning/store/LearningStore.hpp"

#ifdef HECQUIN_WITH_SQLITE
#include <sqlite3.h>
#include <sstream>
#include <string>
#endif

namespace hecquin::learning {

bool LearningStore::run_migrations_() {
#ifndef HECQUIN_WITH_SQLITE
    return false;
#else
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

    // --- v2: outbound API call log (written by LoggingHttpClient decorator). ---
    if (!exec_("CREATE TABLE IF NOT EXISTS api_calls ("
               "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "  ts INTEGER NOT NULL,"
               "  provider TEXT NOT NULL,"
               "  endpoint TEXT NOT NULL,"
               "  method TEXT NOT NULL,"
               "  status INTEGER NOT NULL,"
               "  latency_ms INTEGER NOT NULL,"
               "  request_bytes INTEGER NOT NULL DEFAULT 0,"
               "  response_bytes INTEGER NOT NULL DEFAULT 0,"
               "  ok INTEGER NOT NULL,"
               "  error TEXT"
               ");")) return false;

    if (!exec_("CREATE INDEX IF NOT EXISTS idx_api_calls_ts ON api_calls(ts);"))
        return false;
    if (!exec_("CREATE INDEX IF NOT EXISTS idx_api_calls_provider_ts "
               "ON api_calls(provider, ts);")) return false;

    // --- v2: inbound HTTP request log (written by the Python dashboard middleware). ---
    if (!exec_("CREATE TABLE IF NOT EXISTS request_logs ("
               "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "  ts INTEGER NOT NULL,"
               "  path TEXT NOT NULL,"
               "  method TEXT NOT NULL,"
               "  status INTEGER NOT NULL,"
               "  latency_ms INTEGER NOT NULL,"
               "  remote_ip TEXT,"
               "  user_agent TEXT"
               ");")) return false;

    if (!exec_("CREATE INDEX IF NOT EXISTS idx_request_logs_ts ON request_logs(ts);"))
        return false;

    // --- v3: per-stage voice pipeline events (VAD / Whisper / Piper / drill).
    // Rows are cheap and append-only; the dashboard rolls them up.  Each
    // event carries an opaque JSON payload so schema survives scoring /
    // VAD knob churn without another migration.
    if (!exec_("CREATE TABLE IF NOT EXISTS pipeline_events ("
               "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "  ts INTEGER NOT NULL,"
               "  event TEXT NOT NULL,"         // "vad_gate", "whisper", "piper", ...
               "  outcome TEXT NOT NULL,"       // "ok" | "skipped" | "error"
               "  duration_ms INTEGER NOT NULL DEFAULT 0,"
               "  attrs_json TEXT DEFAULT '{}'"
               ");")) return false;
    if (!exec_("CREATE INDEX IF NOT EXISTS idx_pipeline_events_ts ON pipeline_events(ts);"))
        return false;
    if (!exec_("CREATE INDEX IF NOT EXISTS idx_pipeline_events_event_ts "
               "ON pipeline_events(event, ts);")) return false;

    // --- v3 (Tier-4 #16): per-user namespacing.  Adding a table is
    // backwards-compatible — existing rows in `interactions` /
    // `pronunciation_attempts` / `phoneme_mastery` simply have a NULL
    // user_id, which queries treat as "default user".
    if (!exec_("CREATE TABLE IF NOT EXISTS users ("
               "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "  display_name TEXT NOT NULL UNIQUE,"
               "  voice_embedding_blob BLOB,"
               "  created_at INTEGER NOT NULL"
               ");")) return false;

    // ALTER TABLE ADD COLUMN is safe on existing data — new column is
    // NULL for legacy rows.  We swallow the "duplicate column name"
    // error so re-running on an already-migrated DB is idempotent.
    auto add_user_column = [this](const char* table) {
        std::ostringstream sql;
        sql << "ALTER TABLE " << table << " ADD COLUMN user_id INTEGER REFERENCES users(id);";
        // SQLite has no IF NOT EXISTS for ADD COLUMN; we tolerate the
        // failure that arises on the second run by inspecting
        // PRAGMA table_info first via a quick SELECT.
        std::ostringstream check;
        check << "SELECT 1 FROM pragma_table_info('" << table
              << "') WHERE name='user_id' LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, check.str().c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            const int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (rc == SQLITE_ROW) return true; // already there
        }
        return exec_(sql.str().c_str());
    };
    if (!add_user_column("interactions"))            return false;
    if (!add_user_column("pronunciation_attempts"))  return false;

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

    if (!get_meta_("schema_version")) {
        if (!set_meta_("schema_version", std::to_string(kSchemaVersion))) return false;
    }

    return true;
#endif
}

} // namespace hecquin::learning
