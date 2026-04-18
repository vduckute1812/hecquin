#pragma once

#include <optional>
#include <string>

struct HttpResult {
    long status;
    std::string body;
};

/**
 * POST JSON to `url` with a Bearer token. Returns nullopt on transport-level failure
 * (DNS, timeout, etc.); otherwise the HTTP status + body are always returned.
 */
std::optional<HttpResult> http_post_json(const std::string& url,
                                         const std::string& bearer_token,
                                         const std::string& json_body,
                                         long timeout_seconds = 60);
