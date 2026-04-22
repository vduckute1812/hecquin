// Schema DDL for LearningStore.
//
// All migrations must be idempotent: each CREATE uses IF NOT EXISTS so that
// `run_migrations_()` can be called on every `open()` and new tables added to
// `kSchemaVersion` simply appear on the next launch.

#include "learning/LearningStore.hpp"

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
