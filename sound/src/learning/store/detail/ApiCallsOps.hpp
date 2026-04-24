// Outbound API call + pipeline event free functions.
#pragma once

#ifdef HECQUIN_WITH_SQLITE

#include <string>

struct sqlite3;

namespace hecquin::learning::detail { class StatementCache; }

namespace hecquin::learning::store::detail {

void record_api_call(sqlite3* db,
                     learning::detail::StatementCache& cache,
                     const std::string& provider,
                     const std::string& endpoint,
                     const std::string& method,
                     long status,
                     long latency_ms,
                     long request_bytes,
                     long response_bytes,
                     bool ok,
                     const std::string& error);

void record_pipeline_event(sqlite3* db,
                           learning::detail::StatementCache& cache,
                           const std::string& event,
                           const std::string& outcome,
                           long duration_ms,
                           const std::string& attrs_json);

} // namespace hecquin::learning::store::detail

#endif // HECQUIN_WITH_SQLITE
