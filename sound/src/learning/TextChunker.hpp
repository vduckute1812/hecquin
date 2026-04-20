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

/**
 * Split `text` along line boundaries and greedily pack consecutive non-empty
 * lines into chunks of up to `budget_chars`.  A single line is never split;
 * a line longer than the budget becomes its own (oversize) chunk.  Blank
 * lines act as separators and are dropped.  Trailing `\r` is stripped so the
 * function is CRLF-tolerant.  Lines within a chunk are joined with '\n'.
 *
 * Intended for line-delimited records (JSONL, one-record-per-line dictionaries)
 * where each line is a self-contained semantic unit and character-window
 * slicing would cut records in half.
 */
std::vector<std::string> chunk_lines(const std::string& text, int budget_chars);

} // namespace hecquin::learning
