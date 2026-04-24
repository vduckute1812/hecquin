// Pronunciation drill + phoneme mastery free functions.
#pragma once

#ifdef HECQUIN_WITH_SQLITE

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;

namespace hecquin::learning::detail { class StatementCache; }

namespace hecquin::learning::store::detail {

void record_pronunciation_attempt(sqlite3* db,
                                  learning::detail::StatementCache& cache,
                                  int64_t session_id,
                                  const std::string& reference,
                                  const std::string& transcript,
                                  float pron_overall_0_100,
                                  float intonation_overall_0_100,
                                  const std::string& per_phoneme_json);

void touch_phoneme_mastery(sqlite3* db,
                           learning::detail::StatementCache& cache,
                           const std::vector<std::pair<std::string, float>>& scored);

std::vector<std::string> weakest_phonemes(sqlite3* db,
                                          learning::detail::StatementCache& cache,
                                          int n,
                                          int min_attempts);

} // namespace hecquin::learning::store::detail

#endif // HECQUIN_WITH_SQLITE
