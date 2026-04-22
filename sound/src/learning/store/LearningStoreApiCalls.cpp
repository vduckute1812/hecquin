// Outbound API call log.
//
// Written on every request by the `LoggingHttpClient` decorator in
// `ai/LoggingHttpClient.cpp` (bound via a `std::function` sink to keep the
// `ai` library decoupled from `learning`). Read by the Python dashboard to
// chart daily traffic, latency, and error rates.

#include "learning/store/LearningStore.hpp"

#ifdef HECQUIN_WITH_SQLITE
#include "learning/store/internal/SqliteHelpers.hpp"
#include <sqlite3.h>
#endif

namespace hecquin::learning {

#ifdef HECQUIN_WITH_SQLITE
using detail::prepare_or_log;
using detail::step_done;
using detail::now_epoch_seconds;
#endif

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
    if (!db_) return;
    auto q = prepare_or_log(db_,
        "INSERT INTO api_calls (ts, provider, endpoint, method, status, latency_ms, "
        "  request_bytes, response_bytes, ok, error) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
        "api_calls.record");
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
    if (error.empty()) {
        sqlite3_bind_null(q.get(), 10);
    } else {
        sqlite3_bind_text(q.get(), 10, error.c_str(), -1, SQLITE_TRANSIENT);
    }

    step_done(db_, q.get(), "api_calls.record");
#endif
}

} // namespace hecquin::learning
