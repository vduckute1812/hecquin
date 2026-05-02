#pragma once

#include "ai/LoggingHttpClient.hpp"

namespace hecquin::learning {
class LearningStore;
} // namespace hecquin::learning

namespace hecquin::learning::cli {

/**
 * Returns a sink that forwards each `ApiCallRecord` from
 * `LoggingHttpClient` into `store.record_api_call(...)`.  Shared by
 * `english_tutor` + `english_ingest` instead of duplicating the lambda.
 */
hecquin::ai::ApiCallSink make_store_api_call_sink(
    hecquin::learning::LearningStore& store);

/** Returns a sink that fans every record into both `a` and `b` (empty sides skipped). */
hecquin::ai::ApiCallSink compose_sinks(hecquin::ai::ApiCallSink a,
                                       hecquin::ai::ApiCallSink b);

} // namespace hecquin::learning::cli
