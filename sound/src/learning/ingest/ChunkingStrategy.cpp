#include "learning/ingest/ChunkingStrategy.hpp"

#include "learning/ingest/JsonlChunker.hpp"
#include "learning/ingest/ProseChunker.hpp"

namespace hecquin::learning::ingest {

std::unique_ptr<IChunker> make_chunker_for_extension(const std::string& ext,
                                                     int chunk_chars,
                                                     int chunk_overlap_chars) {
    if (ext == "jsonl" || ext == "json") {
        return std::make_unique<JsonlChunker>(chunk_chars);
    }
    return std::make_unique<ProseChunker>(chunk_chars, chunk_overlap_chars);
}

} // namespace hecquin::learning::ingest
