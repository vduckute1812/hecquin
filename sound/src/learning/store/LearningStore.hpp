#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace hecquin::learning {

namespace detail { class StatementCache; }

struct DocumentRecord {
    int64_t id = 0;
    std::string source;   // file path or dataset slug
    std::string kind;     // vocabulary | grammar | dictionary | readers | custom
    std::string title;
    std::string body;
    std::string metadata_json;
};

struct RetrievedDocument {
    DocumentRecord doc;
    float distance = 0.0f;
};

struct IngestedFile {
    std::string path;
    std::string hash;   // sha-like fingerprint (content-size based, see cpp)
};

/**
 * Thin RAII wrapper around a SQLite database that also loads the sqlite-vec
 * extension (when compiled in) and runs idempotent migrations for the
 * English-learning subsystem.
 *
 * When `HECQUIN_WITH_SQLITE_VEC` is not defined the wrapper falls back to a
 * brute-force cosine search over an embedding BLOB column, so the rest of the
 * pipeline keeps working.
 */
class LearningStore {
public:
    explicit LearningStore(std::string db_path, int embedding_dim = 768);
    ~LearningStore();

    LearningStore(const LearningStore&) = delete;
    LearningStore& operator=(const LearningStore&) = delete;

    /** Open the database and run migrations. Returns false on any failure. */
    bool open();

    bool is_open() const { return db_ != nullptr; }

    /** Insert or replace a document + its embedding; returns the rowid on success. */
    std::optional<int64_t> upsert_document(const DocumentRecord& doc,
                                            const std::vector<float>& embedding);

    /** True if a file was previously ingested with the exact same content fingerprint. */
    bool is_file_already_ingested(const std::string& path, const std::string& hash) const;

    void record_ingested_file(const std::string& path, const std::string& hash);

    /** Run KNN search over the embedding column. */
    std::vector<RetrievedDocument> query_top_k(const std::vector<float>& embedding, int k) const;

    /** Simple convenience: start/end a session. */
    int64_t begin_session(const std::string& mode);
    void end_session(int64_t session_id);

    /** Insert an interaction row (user utterance + tutor reply parts). */
    void record_interaction(int64_t session_id,
                            const std::string& user_text,
                            const std::string& corrected_text,
                            const std::string& grammar_notes);

    /** Bump the `last_seen_at` and auto-create rows for each word token. */
    void touch_vocab(const std::vector<std::string>& words);

    /**
     * Append one pronunciation-drill attempt.  `per_phoneme_json` is the raw
     * JSON document produced by `PronunciationDrillProcessor` — we keep it
     * opaque at the store level so the schema survives scoring-code evolution.
     */
    void record_pronunciation_attempt(int64_t session_id,
                                      const std::string& reference,
                                      const std::string& transcript,
                                      float pron_overall_0_100,
                                      float intonation_overall_0_100,
                                      const std::string& per_phoneme_json);

    /** Update the phoneme-mastery roll-up table for a batch of observed phonemes. */
    void touch_phoneme_mastery(const std::vector<std::pair<std::string, float>>& scored);

    /** Read up to `limit` example drill sentences stored in documents(kind='drill'). */
    std::vector<std::string> sample_drill_sentences(int limit) const;

    /**
     * Return up to `n` IPA phoneme tokens with the lowest rolling `avg_score`,
     * restricted to rows that have been observed at least `min_attempts` times
     * (so a single bad sample does not dominate the picker).  Sorted worst-
     * first; empty when `phoneme_mastery` has no qualifying rows.
     */
    std::vector<std::string> weakest_phonemes(int n, int min_attempts = 2) const;

    /**
     * Tier-4 #16: insert-or-find the user row for `display_name`.
     * Returns the row's `id` (or `nullopt` on DB error).  Keeps the
     * single source of truth out of the listener / app layer — they
     * just call `upsert_user("Mia")` and stash the returned id in
     * their own state.
     */
    std::optional<int64_t> upsert_user(const std::string& display_name);

    /**
     * Tier-4 #17: average pronunciation score across the most recent
     * `limit` `pronunciation_attempts` rows (entire table by default).
     * Returns `nullopt` when no attempts have been recorded yet.
     * Optionally filtered to a specific `user_id` for the recap line.
     */
    std::optional<float> last_session_pronunciation_score(
        int limit = 10,
        std::optional<int64_t> user_id = std::nullopt) const;

    /**
     * Append one outbound API call log row (LLM/embedding/etc.). Written by the
     * C++ `LoggingHttpClient` decorator on every request; read by the dashboard
     * module to chart daily traffic, latency, and error rates.
     *
     * `error` is the empty string on success; non-empty values are surfaced as
     * `ok=0` rows so the dashboard can compute failure rates without joining.
     */
    void record_api_call(const std::string& provider,
                         const std::string& endpoint,
                         const std::string& method,
                         long status,
                         long latency_ms,
                         long request_bytes,
                         long response_bytes,
                         bool ok,
                         const std::string& error);

    /**
     * Append one internal pipeline event (VAD skip/pass, Whisper latency,
     * Piper synth duration, drill alignment ok/fail, …).  `attrs_json` is
     * an opaque JSON blob rendered by the caller — intended for extra
     * per-event attributes (e.g. `{"no_speech_prob": 0.12}` on Whisper
     * events).  Empty string stores `"{}"`.
     */
    void record_pipeline_event(const std::string& event,
                               const std::string& outcome,
                               long duration_ms,
                               const std::string& attrs_json);

    const std::string& db_path() const { return db_path_; }
    int embedding_dim() const { return embedding_dim_; }

    /** Current schema version this build writes. Bump whenever DDL changes. */
    static constexpr int kSchemaVersion = 3;

    /**
     * @internal — statement cache shared across the LearningStore*.cpp TUs.
     * Not part of the public API; do not consume from outside learning/.
     */
    detail::StatementCache* stmt_cache() const { return stmt_cache_.get(); }

private:
    bool run_migrations_();
    bool exec_(const char* sql);
    bool check_embedding_dim_();
    bool set_meta_(const char* key, const std::string& value);
    std::optional<std::string> get_meta_(const char* key) const;

    std::string db_path_;
    int embedding_dim_;
    sqlite3* db_ = nullptr;
    bool has_vec0_ = false;
    // Declared as `unique_ptr` so the header doesn't need to know the full
    // type.  Destroyed in ~LearningStore *before* the sqlite3 handle.
    mutable std::unique_ptr<detail::StatementCache> stmt_cache_;
};

} // namespace hecquin::learning
