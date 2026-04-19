#pragma once

#include <string>
#include <vector>

namespace hecquin::learning {

/**
 * Split `text` into UTF-8 chunks of approximately `chunk_chars` characters
 * with `overlap` characters of context carried into the next chunk.
 *
 * The split prefers whitespace boundaries in the second half of each chunk
 * so words are not cut mid-syllable.  Chunks are trimmed of leading and
 * trailing whitespace; empty chunks are dropped.
 *
 * Pure, stateless, allocator-free beyond the returned vector — exposed from
 * its own header so `Ingestor` can use it and tests can assert boundary
 * behaviour without spinning up the full ingestion pipeline.
 */
std::vector<std::string> chunk_text(const std::string& text, int chunk_chars, int overlap);

} // namespace hecquin::learning
