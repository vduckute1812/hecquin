// Session + interaction + vocab progress free functions.
#pragma once

#ifdef HECQUIN_WITH_SQLITE

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace hecquin::learning::detail { class StatementCache; }

namespace hecquin::learning::store::detail {

int64_t begin_session(sqlite3* db,
                      learning::detail::StatementCache& cache,
                      const std::string& mode);

void end_session(sqlite3* db,
                 learning::detail::StatementCache& cache,
                 int64_t session_id);

void record_interaction(sqlite3* db,
                        learning::detail::StatementCache& cache,
                        int64_t session_id,
                        const std::string& user_text,
                        const std::string& corrected_text,
                        const std::string& grammar_notes);

void touch_vocab(sqlite3* db,
                 learning::detail::StatementCache& cache,
                 const std::vector<std::string>& words);

} // namespace hecquin::learning::store::detail

#endif // HECQUIN_WITH_SQLITE
