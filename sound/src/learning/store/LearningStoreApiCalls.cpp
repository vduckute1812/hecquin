// Outbound API call log.
//
// Written on every request by the `LoggingHttpClient` decorator in
// `ai/LoggingHttpClient.cpp` (bound via a `std::function` sink to keep the
// `ai` library decoupled from `learning`). Read by the Python dashboard to
// chart daily traffic, latency, and error rates.

#include "learning/store/LearningStore.hpp"

#ifdef HECQUIN_WITH_SQLITE
#include "learning/store/detail/ApiCallsOps.hpp"
#include "learning/store/internal/SqliteHelpers.hpp"
#include <sqlite3.h>
#endif

namespace hecquin::learning {

#ifdef HECQUIN_WITH_SQLITE

namespace store::detail {

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
                     const std::string& error) {
    using learning::detail::step_done;
    using learning::detail::now_epoch_seconds;
    if (!db) return;
    auto q = cache.acquire("api_calls.record",
        "INSERT INTO api_calls (ts, provider, endpoint, method, status, latency_ms, "
        "  request_bytes, response_bytes, ok, error) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    if (!q) return;

    sqlite3_bind_int64(q.get(), 1, now_epoch_seconds());
    sqlite3_bind_text(q.get(),  2, provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.get(),  3, endpoint.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.get(),  4, method.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.get(), 5, static_cast<sqlite3_int64>(status));
    sqlite3_bind_int64(q.get(), 6, static_cast<sqlite3_int64>(latency_ms));
    sqlite3_bind_int64(q.get(), 7, static_cast<sqlite3_int64>(request_bytes));
    sqlite3_bind_int64(q.get(), 8, static_cast<sqlite3_int64>(response_bytes));
    sqlite3_bind_int(q.get(),   9, ok ? 1 : 0);
    if (error.empty()) sqlite3_bind_null(q.get(), 10);
    else               sqlite3_bind_text(q.get(), 10, error.c_str(), -1, SQLITE_TRANSIENT);

    step_done(db, q.get(), "api_calls.record");
}

void record_pipeline_event(sqlite3* db,
                           learning::detail::StatementCache& cache,
                           const std::string& event,
                           const std::string& outcome,
                           long duration_ms,
                           const std::string& attrs_json) {
    using learning::detail::step_done;
    using learning::detail::now_epoch_seconds;
    if (!db) return;
    auto q = cache.acquire("pipeline_events.record",
        "INSERT INTO pipeline_events (ts, event, outcome, duration_ms, attrs_json) "
        "VALUES (?, ?, ?, ?, ?);");
    if (!q) return;
    sqlite3_bind_int64(q.get(), 1, now_epoch_seconds());
    sqlite3_bind_text(q.get(),  2, event.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q.get(),  3, outcome.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(q.get(), 4, static_cast<sqlite3_int64>(duration_ms));
    if (attrs_json.empty()) sqlite3_bind_text(q.get(), 5, "{}", -1, SQLITE_STATIC);
    else                    sqlite3_bind_text(q.get(), 5, attrs_json.c_str(), -1, SQLITE_TRANSIENT);
    step_done(db, q.get(), "pipeline_events.record");
}

} // namespace store::detail

#endif // HECQUIN_WITH_SQLITE

void LearningStore::record_api_call(const std::string& provider,
                                    const std::string& endpoint,
                                    const std::string& method,
                                    long status,
                                    long latency_ms,
                                    long request_bytes,
                                    long response_bytes,
                                    bool ok,
                                    const std::string& error) {
#ifndef HECQUIN_WITH_SQLITE
    (void)provider; (void)endpoint; (void)method; (void)status; (void)latency_ms;
    (void)request_bytes; (void)response_bytes; (void)ok; (void)error;
#else
    if (!stmt_cache_) return;
    store::detail::record_api_call(db_, *stmt_cache_, provider, endpoint, method,
                                   status, latency_ms, request_bytes, response_bytes,
                                   ok, error);
#endif
}

void LearningStore::record_pipeline_event(const std::string& event,
                                          const std::string& outcome,
                                          long duration_ms,
                                          const std::string& attrs_json) {
#ifndef HECQUIN_WITH_SQLITE
    (void)event; (void)outcome; (void)duration_ms; (void)attrs_json;
#else
    if (!stmt_cache_) return;
    store::detail::record_pipeline_event(db_, *stmt_cache_, event, outcome,
                                         duration_ms, attrs_json);
#endif
}

} // namespace hecquin::learning
