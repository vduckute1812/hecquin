#pragma once

#include "learning/ingest/ChunkingStrategy.hpp"

namespace hecquin::learning::ingest {

/**
 * Prose / markdown chunker.  Delegates to the existing `chunk_text`
 * helper in `TextChunker.hpp` — kept as a standalone class so the
 * factory can swap it with other strategies (e.g. a future
 * MarkdownChunker that respects heading boundaries).
 */
class ProseChunker : public IChunker {
public:
    ProseChunker(int chunk_chars, int chunk_overlap_chars);
    std::vector<std::string> chunk(const std::string& content) const override;

private:
    int chunk_chars_;
    int chunk_overlap_chars_;
};

} // namespace hecquin::learning::ingest
