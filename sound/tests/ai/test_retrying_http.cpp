// Unit tests for RetryingHttpClient: verify it retries on transient
// failures, stops after `max_attempts`, and passes through terminal
// responses (2xx, 4xx) untouched.

#include "ai/IHttpClient.hpp"
#include "ai/RetryingHttpClient.hpp"

#include <chrono>
#include <cstdio>
#include <deque>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

int fail(const char* message) {
    std::cerr << "[test_retrying_http] FAIL: " << message << std::endl;
    return 1;
}

/**
 * Scripted HTTP backend: dequeues one canned response per call.  `nullopt`
 * simulates a transport failure.  Tracks the number of calls so tests can
 * assert the retrier stopped where it was supposed to.
 */
class ScriptedHttp final : public hecquin::ai::IHttpClient {
public:
    std::deque<std::optional<HttpResult>> canned;
    int calls = 0;

    std::optional<HttpResult> post_json(const std::string& /*url*/,
                                        const std::string& /*bearer*/,
                                        const std::string& /*body*/,
                                        long /*timeout*/) override {
        ++calls;
        if (canned.empty()) return std::nullopt;
        auto v = canned.front();
        canned.pop_front();
        return v;
    }
};

void instant_sleep(std::chrono::milliseconds /*ms*/) {}

} // namespace

int main() {
    using hecquin::ai::RetryingHttpClient;
    using hecquin::ai::RetryPolicy;

    // 1. 2xx passes through on the first attempt.
    {
        ScriptedHttp inner;
        inner.canned.push_back(HttpResult{200, "ok"});
        RetryingHttpClient http(inner, RetryPolicy{3, std::chrono::milliseconds(1),
                                                   std::chrono::milliseconds(1), 2.0, 0.0});
        http.set_sleep_fn_for_test(&instant_sleep);
        auto r = http.post_json("u", "t", "{}");
        if (!r || r->status != 200) return fail("200 should pass through");
        if (inner.calls != 1) return fail("200 should not retry");
    }

    // 2. 404 (non-transient) passes through on the first attempt.
    {
        ScriptedHttp inner;
        inner.canned.push_back(HttpResult{404, "no"});
        RetryingHttpClient http(inner, RetryPolicy{3, std::chrono::milliseconds(1),
                                                   std::chrono::milliseconds(1), 2.0, 0.0});
        http.set_sleep_fn_for_test(&instant_sleep);
        auto r = http.post_json("u", "t", "{}");
        if (!r || r->status != 404) return fail("404 should pass through");
        if (inner.calls != 1) return fail("404 should not retry");
    }

    // 3. 500 then 200 → retrier recovers after one retry.
    {
        ScriptedHttp inner;
        inner.canned.push_back(HttpResult{500, "oops"});
        inner.canned.push_back(HttpResult{200, "yay"});
        RetryingHttpClient http(inner, RetryPolicy{3, std::chrono::milliseconds(1),
                                                   std::chrono::milliseconds(1), 2.0, 0.0});
        http.set_sleep_fn_for_test(&instant_sleep);
        auto r = http.post_json("u", "t", "{}");
        if (!r || r->status != 200) return fail("500 then 200 should land on 200");
        if (inner.calls != 2) return fail("500 then 200 should hit backend twice");
    }

    // 4. All 429 → returns the final 429 and stops at max_attempts.
    {
        ScriptedHttp inner;
        for (int i = 0; i < 5; ++i) inner.canned.push_back(HttpResult{429, "slow down"});
        RetryingHttpClient http(inner, RetryPolicy{3, std::chrono::milliseconds(1),
                                                   std::chrono::milliseconds(1), 2.0, 0.0});
        http.set_sleep_fn_for_test(&instant_sleep);
        auto r = http.post_json("u", "t", "{}");
        if (!r || r->status != 429) return fail("all-429 must return 429");
        if (inner.calls != 3) return fail("must stop at max_attempts=3");
    }

    // 5. Transport failure (nullopt) retries up to max_attempts.
    {
        ScriptedHttp inner;
        inner.canned.push_back(std::nullopt);
        inner.canned.push_back(std::nullopt);
        inner.canned.push_back(HttpResult{200, "finally"});
        RetryingHttpClient http(inner, RetryPolicy{3, std::chrono::milliseconds(1),
                                                   std::chrono::milliseconds(1), 2.0, 0.0});
        http.set_sleep_fn_for_test(&instant_sleep);
        auto r = http.post_json("u", "t", "{}");
        if (!r || r->status != 200) return fail("transport→transport→200 should land on 200");
        if (inner.calls != 3) return fail("two transport failures, then 200");
    }

    std::cout << "[test_retrying_http] OK" << std::endl;
    return 0;
}
