#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace hecquin::learning {
class LearningStore;
} // namespace hecquin::learning

namespace hecquin::learning::ingest {

/**
 * Builds `DocumentRecord` instances for each chunk and forwards them to
 * `LearningStore::upsert_document`.  Extracted from the old
 * `persist_batch` lambda inside `Ingestor::ingest_file_` so the record
 * assembly rule has one home — and so the ingestor doesn't need to
 * know about SQLite schema details.
 */
class DocumentPersister {
public:
    struct FileParams {
        std::string source;
        std::string kind;
        std::string title_prefix;
        std::size_t total_chunks = 0;
    };

    explicit DocumentPersister(LearningStore& store);

    /** Persist one chunk at absolute index `idx` with its embedding. */
    bool persist(const FileParams& params,
                 std::size_t idx,
                 const std::string& body,
                 const std::vector<float>& embedding);

private:
    LearningStore& store_;
};

} // namespace hecquin::learning::ingest
