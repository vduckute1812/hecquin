#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace hecquin::learning {

class LearningStore;
class EmbeddingClient;

struct IngestReport {
    int files_scanned = 0;
    int files_skipped = 0;
    int files_ingested = 0;
    int chunks_written = 0;
    int chunks_failed = 0;
};

struct IngestorConfig {
    std::string curriculum_dir = ".env/shared/learning/curriculum";
    std::string custom_dir = ".env/shared/learning/custom";
    int chunk_chars = 1800;       // ~400 tokens of English text
    int chunk_overlap_chars = 200;
    bool force_rebuild = false;

    /**
     * How many chunks to pack into a single `/embeddings` request.  Gemini's
     * OpenAI-compat endpoint caps array length around 100; 16 is a safe
     * default that still yields ~10× speed-up over single-item requests.
     * Set to 1 to disable batching (useful when debugging a specific chunk).
     */
    int embed_batch_size = 16;
};

/**
 * Scans curriculum / custom directories, skips unchanged files (by content hash),
 * chunks text, embeds each chunk through `EmbeddingClient`, and upserts into `LearningStore`.
 */
class Ingestor {
public:
    Ingestor(LearningStore& store, EmbeddingClient& embed, IngestorConfig cfg = {});

    /** Run a full pass. Prints progress to stderr. */
    IngestReport run();

private:
    void ingest_dir_(const std::string& dir, const std::string& default_kind,
                     IngestReport& report);
    void ingest_file_(const std::string& path, const std::string& kind,
                      IngestReport& report);

    LearningStore& store_;
    EmbeddingClient& embed_;
    IngestorConfig cfg_;

    // Progress counters (updated by run()).
    size_t total_files_ = 0;
    size_t file_index_ = 0;
};

} // namespace hecquin::learning
