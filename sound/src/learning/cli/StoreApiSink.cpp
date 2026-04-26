#include "learning/cli/StoreApiSink.hpp"

#include "learning/store/LearningStore.hpp"

namespace hecquin::learning::cli {

hecquin::ai::ApiCallSink make_store_api_call_sink(
    hecquin::learning::LearningStore& store) {
    return [&store](const hecquin::ai::ApiCallRecord& r) {
        store.record_api_call(r.provider, r.endpoint, r.method,
                              r.status, r.latency_ms,
                              r.request_bytes, r.response_bytes,
                              r.ok, r.error);
    };
}

} // namespace hecquin::learning::cli
