#pragma once

#include "ai/IHttpClient.hpp"

#include <functional>
#include <optional>
#include <string>

namespace hecquin::ai {

/**
 * One outbound HTTP call worth logging. Collected by `LoggingHttpClient` and
 * forwarded to an `ApiCallSink` (typically bound to `LearningStore::record_api_call`).
 *
 * Kept as a plain struct so the `ai/` library stays free of any `learning/`
 * dependency; the decorator observes a call and hands a record to whatever
 * sink the top-level application wired in.
 */
struct ApiCallRecord {
    std::string provider;
    std::string endpoint;
    std::string method;        // always "POST" from the current IHttpClient surface
    long status = 0;           // HTTP status; 0 when the transport failed
    long latency_ms = 0;       // wall-clock duration of the round-trip
    long request_bytes = 0;    // size of the JSON body we sent
    long response_bytes = 0;   // size of the response body (0 on transport failure)
    bool ok = false;           // 2xx and no transport error
    std::string error;         // populated on transport failure / non-2xx status
};

using ApiCallSink = std::function<void(const ApiCallRecord&)>;

/**
 * Decorator over any `IHttpClient` that times each request and forwards a
 * summary row to `ApiCallSink`. The delegated call's return value is passed
 * through untouched so existing callers (ChatClient, EmbeddingClient, …) keep
 * their exact semantics.
 *
 * Usage:
 *
 *     CurlHttpClient raw;
 *     LoggingHttpClient http(raw, "openai",
 *                            [&store](const ApiCallRecord& r) {
 *                                store.record_api_call(r.provider, r.endpoint,
 *                                    r.method, r.status, r.latency_ms,
 *                                    r.request_bytes, r.response_bytes,
 *                                    r.ok, r.error);
 *                            });
 *     ChatClient chat(cfg, http);
 *
 * The sink runs inline on the caller's thread; keep it cheap. If the sink is
 * empty (default-constructed `std::function`) the decorator degrades to a pure
 * passthrough.
 */
class LoggingHttpClient final : public IHttpClient {
public:
    LoggingHttpClient(IHttpClient& inner, std::string provider, ApiCallSink sink);

    std::optional<HttpResult> post_json(const std::string& url,
                                        const std::string& bearer_token,
                                        const std::string& json_body,
                                        long timeout_seconds = 60) override;

private:
    IHttpClient& inner_;
    std::string  provider_;
    ApiCallSink  sink_;
};

} // namespace hecquin::ai
