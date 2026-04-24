#include "ai/RetryingHttpClient.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <random>
#include <thread>

namespace hecquin::ai {

namespace {

bool parse_long_env(const char* name, long& out) {
    const char* raw = std::getenv(name);
    if (!raw || *raw == '\0') return false;
    try {
        out = std::stol(raw);
        return true;
    } catch (...) {
        std::cerr << "[retry] ignoring invalid " << name << "=" << raw << std::endl;
        return false;
    }
}

void default_sleep(std::chrono::milliseconds ms) {
    std::this_thread::sleep_for(ms);
}

double uniform_jitter() {
    // `thread_local` keeps the RNG state in-thread so two concurrent retries
    // don't both see the same jitter (which would defeat the point).
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    return dist(rng);
}

} // namespace

void RetryPolicy::apply_env_overrides() {
    long v = 0;
    if (parse_long_env("HECQUIN_HTTP_RETRY_MAX", v) && v > 0) {
        max_attempts = static_cast<std::size_t>(v);
    }
    if (parse_long_env("HECQUIN_HTTP_RETRY_BASE_MS", v) && v >= 0) {
        base_delay = std::chrono::milliseconds(v);
    }
    if (parse_long_env("HECQUIN_HTTP_RETRY_MAX_MS", v) && v >= 0) {
        max_delay = std::chrono::milliseconds(v);
    }
}

RetryingHttpClient::RetryingHttpClient(IHttpClient& inner, RetryPolicy policy)
    : inner_(inner), policy_(policy), sleep_(&default_sleep) {}

bool RetryingHttpClient::is_transient_status_(long status) {
    // 408 = request timeout, 429 = too many requests, 5xx = upstream issue.
    return status == 408 || status == 429 || (status >= 500 && status < 600);
}

std::chrono::milliseconds RetryingHttpClient::delay_for_attempt_(std::size_t attempt_index) const {
    // attempt_index is 0-based (0 = the delay before the *second* attempt).
    double ms = static_cast<double>(policy_.base_delay.count());
    for (std::size_t i = 0; i < attempt_index; ++i) {
        ms *= policy_.backoff_multiplier;
    }
    const double cap = static_cast<double>(policy_.max_delay.count());
    ms = std::min(ms, cap);
    if (policy_.jitter_ratio > 0.0) {
        ms *= (1.0 + policy_.jitter_ratio * uniform_jitter());
    }
    if (ms < 0.0) ms = 0.0;
    return std::chrono::milliseconds(static_cast<long long>(ms));
}

std::optional<HttpResult> RetryingHttpClient::post_json(const std::string& url,
                                                       const std::string& bearer_token,
                                                       const std::string& json_body,
                                                       long timeout_seconds) {
    std::optional<HttpResult> result;
    const std::size_t attempts = std::max<std::size_t>(1, policy_.max_attempts);
    for (std::size_t i = 0; i < attempts; ++i) {
        result = inner_.post_json(url, bearer_token, json_body, timeout_seconds);
        const bool transient =
            !result || is_transient_status_(result->status);
        const bool last = (i + 1 == attempts);
        if (!transient || last) break;

        const auto delay = delay_for_attempt_(i);
        std::cerr << "[retry] attempt " << (i + 1) << "/" << attempts;
        if (result) {
            std::cerr << " got HTTP " << result->status;
        } else {
            std::cerr << " transport failure";
        }
        std::cerr << ", retrying in " << delay.count() << " ms" << std::endl;
        if (sleep_) sleep_(delay);
    }
    return result;
}

} // namespace hecquin::ai
