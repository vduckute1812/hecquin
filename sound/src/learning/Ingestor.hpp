#pragma once

#include "learning/ingest/RunBreaker.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace hecquin::learning {

class LearningStore;
class EmbeddingClient;

namespace ingest { struct PlannedFile; }

struct IngestReport {
    int files_scanned = 0;
    int files_skipped = 0;
    int files_ingested = 0;
    int files_pruned = 0;
    int chunks_written = 0;
    int chunks_failed = 0;
};

struct IngestorConfig {
    std::string curriculum_dir = ".env/shared/learning/curriculum";
    std::string custom_dir = ".env/shared/learning/custom";
    int chunk_chars = 1800;       // ~400 tokens of English text
    int chunk_overlap_chars = 200;
    bool force_rebuild = false;

    /** Chunks per `/embeddings` request. CLI: `--batch-size`. */
    int embed_batch_size = 16;

    /** Drop DB rows whose source vanished from disk. CLI: `--prune-missing`. */
    bool prune_missing_sources = false;

    /** Abort after N back-to-back failed chunks. 0 disables. CLI: `--max-fail-streak`. */
    int max_consecutive_chunk_failures = 50;

    /** Whole-run deadline in seconds. 0 = unbounded. CLI: `--deadline`. */
    int run_deadline_seconds = 0;

    /** Skip files larger than this (bytes). 0 disables. CLI: `--max-file-bytes`. */
    long long max_file_bytes = 50LL * 1024 * 1024;
};

/**
 * Coordinator for the ingest pipeline (FileDiscovery → ContentFingerprint →
 * ChunkingStrategy → EmbeddingBatcher → DocumentPersister → ProgressReporter).
 */
class Ingestor {
public:
    Ingestor(LearningStore& store, EmbeddingClient& embed, IngestorConfig cfg = {});

    /** Run a full pass. Prints progress to stderr. */
    IngestReport run();

private:
    /** Per-file state carried between the three pipeline stages. */
    struct FilePlan {
        std::string path;
        std::string source_id;
        std::string title;
        std::string kind;
        std::string hash;
        std::vector<std::string> chunks;
        std::size_t content_size = 0;
    };

    /** Result of the embed-and-persist stage for a single file. */
    struct ChunkOutcome {
        int ok = 0;
        int failed = 0;
    };

    /** Sentinel run that hard-stops on dim mismatch; returns true iff safe to continue. */
    bool probe_embedding_dim_();

    void ingest_file_(const std::string& path, const std::string& kind,
                      IngestReport& report);

    /** Stat / read / fingerprint / chunk. nullopt = file already counted as skipped. */
    std::optional<FilePlan> prepare_file_(const std::string& path,
                                          const std::string& kind,
                                          IngestReport& report);

    ChunkOutcome embed_and_persist_(const FilePlan& plan, IngestReport& report);

    void commit_or_rollback_(const FilePlan& plan, ChunkOutcome outcome,
                             int chunks_failed_at_entry, IngestReport& report);

    void prune_missing_sources_(const std::vector<ingest::PlannedFile>& plan,
                                IngestReport& report);

    LearningStore& store_;
    EmbeddingClient& embed_;
    IngestorConfig cfg_;

    std::size_t total_files_ = 0;
    std::size_t file_index_ = 0;

    std::optional<ingest::RunBreaker> breaker_;
};

} // namespace hecquin::learning
