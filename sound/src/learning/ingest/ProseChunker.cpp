#include "learning/ingest/ProseChunker.hpp"

#include "learning/TextChunker.hpp"

namespace hecquin::learning::ingest {

ProseChunker::ProseChunker(int chunk_chars, int chunk_overlap_chars)
    : chunk_chars_(chunk_chars), chunk_overlap_chars_(chunk_overlap_chars) {}

std::vector<std::string> ProseChunker::chunk(const std::string& content) const {
    return chunk_text(content, chunk_chars_, chunk_overlap_chars_);
}

} // namespace hecquin::learning::ingest
