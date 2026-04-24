#pragma once

#include "ai/IHttpClient.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

namespace hecquin::ai {

/**
 * Retry policy for transient HTTP failures.
 *
 * "Transient" means: transport-level failure (DNS / timeout / TLS),
 * HTTP 408 (request timeout), HTTP 429 (rate limit), or HTTP 5xx.  Every
 * other status is returned immediately — retrying a 4xx almost never helps
 * and costs the user tokens.
 *
 * Delays follow exponential backoff with a small random jitter to spread
 * out thundering herds when multiple clients are rate-limited at once.
 */
struct RetryPolicy {
    std::size_t max_attempts = 3;          ///< total attempts including the first
    std::chrono::milliseconds base_delay{250};
    std::chrono::milliseconds max_delay{2000};
    double backoff_multiplier = 2.0;
    /** Jitter as a fraction of the computed delay, applied as ±jitter_ratio. */
    double jitter_ratio = 0.2;

    /** Populate from env (HECQUIN_HTTP_RETRY_MAX / _BASE_MS / _MAX_MS). */
    void apply_env_overrides();
};

/**
 * Decorator that wraps any `IHttpClient` in a retry loop.
 *
 * Usage:
 *     CurlHttpClient raw;
 *     LoggingHttpClient logged(raw, "openai", sink);
 *     RetryingHttpClient http(logged, RetryPolicy{});
 *     ChatClient chat(cfg, http);
 *
 * Keep the retrier *outside* the logger so each attempt shows up as its own
 * row in `api_calls` — that makes rate-limit churn visible on the dashboard.
 */
class RetryingHttpClient final : public IHttpClient {
public:
    RetryingHttpClient(IHttpClient& inner, RetryPolicy policy = {});

    std::optional<HttpResult> post_json(const std::string& url,
                                        const std::string& bearer_token,
                                        const std::string& json_body,
                                        long timeout_seconds = 60) override;

    /** Exposed for tests to inject a deterministic sleep (no real waiting). */
    using SleepFn = void (*)(std::chrono::milliseconds);
    void set_sleep_fn_for_test(SleepFn fn) { sleep_ = fn; }

private:
    static bool is_transient_status_(long status);
    std::chrono::milliseconds delay_for_attempt_(std::size_t attempt_index) const;

    IHttpClient&  inner_;
    RetryPolicy   policy_;
    SleepFn       sleep_ = nullptr;   // default = std::this_thread::sleep_for
};

} // namespace hecquin::ai
