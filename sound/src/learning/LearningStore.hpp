#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace hecquin::learning {

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

    const std::string& db_path() const { return db_path_; }
    int embedding_dim() const { return embedding_dim_; }

    /** Current schema version this build writes. Bump whenever DDL changes. */
    static constexpr int kSchemaVersion = 1;

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
};

} // namespace hecquin::learning
