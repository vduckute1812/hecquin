#pragma once

#include <string>

namespace hecquin::learning::Vocabulary {

/**
 * Lowercase + apostrophe-preserving token normaliser.
 *
 * Rules (shared by `ProgressTracker::tokenize` and the in-loop filter
 * inside `touch_vocab`):
 *   - keeps ASCII alphabetic characters and the apostrophe `'`;
 *   - lowercases alphabetic characters;
 *   - drops every other byte (punctuation, whitespace, digits, UTF-8
 *     multi-byte sequences).
 *
 * Returns the normalised token.  Callers are expected to enforce a
 * minimum length (both current call sites require `>= 2`).
 */
std::string normalise(const std::string& word);

} // namespace hecquin::learning::Vocabulary
