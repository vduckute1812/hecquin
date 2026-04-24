#pragma once

#include <memory>
#include <string>
#include <vector>

namespace hecquin::learning::ingest {

/**
 * Strategy interface for splitting a file's raw content into embeddable
 * chunks.  Different file formats (free prose, JSONL) want different
 * splitting rules; choosing the strategy at the edge of `ingest_file_`
 * keeps the orchestrator flat.
 */
class IChunker {
public:
    virtual ~IChunker() = default;
    virtual std::vector<std::string> chunk(const std::string& content) const = 0;
};

/** Pick the right strategy for a given file extension. */
std::unique_ptr<IChunker> make_chunker_for_extension(const std::string& ext,
                                                     int chunk_chars,
                                                     int chunk_overlap_chars);

} // namespace hecquin::learning::ingest
