#include "learning/ingest/JsonlChunker.hpp"

#include "learning/TextChunker.hpp"

namespace hecquin::learning::ingest {

JsonlChunker::JsonlChunker(int chunk_chars) : chunk_chars_(chunk_chars) {}

std::vector<std::string> JsonlChunker::chunk(const std::string& content) const {
    // `.json` files are often single-line; split via prose chunker if oversized.
    const auto first_nl = content.find('\n');
    const bool single_line =
        first_nl == std::string::npos ||
        content.find('\n', first_nl + 1) == std::string::npos;
    if (single_line && content.size() > static_cast<std::size_t>(chunk_chars_)) {
        return chunk_text(content, chunk_chars_, /*overlap=*/0);
    }
    return chunk_lines(content, chunk_chars_);
}

} // namespace hecquin::learning::ingest
