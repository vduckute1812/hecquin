#pragma once

#include "ai/LoggingHttpClient.hpp"

#include <string>

namespace hecquin::learning::cli {

/**
 * Append one CSV row per `ApiCallRecord` to `path`. Empty path or open
 * failure returns an empty std::function (sink is then a silent no-op).
 * Header is written only if the file did not exist or was empty.
 */
hecquin::ai::ApiCallSink make_csv_api_call_sink(const std::string& path);

/** Header line written to a fresh file. Exposed for tests. */
inline const char* csv_api_call_header() {
    return "timestamp_iso8601,provider,endpoint,method,status,latency_ms,"
           "request_bytes,response_bytes,ok,error";
}

} // namespace hecquin::learning::cli
