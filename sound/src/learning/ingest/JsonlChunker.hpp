#pragma once

#include "learning/ingest/ChunkingStrategy.hpp"

namespace hecquin::learning::ingest {

/**
 * Line-oriented chunker for JSONL / JSON files.  Delegates to
 * `chunk_lines` in `TextChunker.hpp`, which preserves line boundaries
 * so downstream parsers can still read one JSON object per chunk.
 */
class JsonlChunker : public IChunker {
public:
    explicit JsonlChunker(int chunk_chars);
    std::vector<std::string> chunk(const std::string& content) const override;

private:
    int chunk_chars_;
};

} // namespace hecquin::learning::ingest
