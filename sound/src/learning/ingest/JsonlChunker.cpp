#include "learning/ingest/JsonlChunker.hpp"

#include "learning/TextChunker.hpp"

namespace hecquin::learning::ingest {

JsonlChunker::JsonlChunker(int chunk_chars) : chunk_chars_(chunk_chars) {}

std::vector<std::string> JsonlChunker::chunk(const std::string& content) const {
    return chunk_lines(content, chunk_chars_);
}

} // namespace hecquin::learning::ingest
