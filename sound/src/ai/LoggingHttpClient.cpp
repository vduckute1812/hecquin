#include "ai/LoggingHttpClient.hpp"

#include <chrono>
#include <utility>

namespace hecquin::ai {

namespace {

long elapsed_ms_since(std::chrono::steady_clock::time_point t0) {
    const auto delta = std::chrono::steady_clock::now() - t0;
    return static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(delta).count());
}

} // namespace

LoggingHttpClient::LoggingHttpClient(IHttpClient& inner,
                                     std::string provider,
                                     ApiCallSink sink)
    : inner_(inner),
      provider_(std::move(provider)),
      sink_(std::move(sink)) {}

std::optional<HttpResult> LoggingHttpClient::post_json(const std::string& url,
                                                      const std::string& bearer_token,
                                                      const std::string& json_body,
                                                      long timeout_seconds) {
    const auto started = std::chrono::steady_clock::now();
    auto result = inner_.post_json(url, bearer_token, json_body, timeout_seconds);

    if (!sink_) return result;

    ApiCallRecord rec;
    rec.provider      = provider_;
    rec.endpoint      = url;
    rec.method        = "POST";
    rec.latency_ms    = elapsed_ms_since(started);
    rec.request_bytes = static_cast<long>(json_body.size());

    if (!result) {
        // Transport-level failure (DNS, timeout, TLS, …). status stays 0.
        rec.ok    = false;
        rec.error = "transport_failure";
    } else {
        rec.status         = result->status;
        rec.response_bytes = static_cast<long>(result->body.size());
        rec.ok             = (result->status >= 200 && result->status < 300);
        if (!rec.ok) {
            rec.error = "http_" + std::to_string(result->status);
        }
    }

    // Never let logging bring down the actual HTTP path.
    try {
        sink_(rec);
    } catch (...) {
        // swallow — observability must not affect behaviour
    }

    return result;
}

} // namespace hecquin::ai
