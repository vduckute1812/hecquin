#pragma once

#include "ai/HttpClient.hpp"

#include <optional>
#include <string>

namespace hecquin::ai {

/**
 * Interface over an HTTP POST-JSON call.  Lets tests inject canned responses
 * and lets the production code swap transports (libcurl, CFNetwork, …)
 * without touching the consumers.
 */
class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    /**
     * POST `json_body` to `url` with `Authorization: Bearer <bearer_token>`.
     * Returns `nullopt` on transport-level failure; otherwise the HTTP status
     * and response body are always filled (even for non-2xx responses).
     */
    virtual std::optional<HttpResult> post_json(const std::string& url,
                                                const std::string& bearer_token,
                                                const std::string& json_body,
                                                long timeout_seconds = 60) = 0;
};

/**
 * Thin adapter over the free `http_post_json` built on libcurl.  Construct
 * one per CLI / service — libcurl global state is shared behind the function.
 */
class CurlHttpClient final : public IHttpClient {
public:
    std::optional<HttpResult> post_json(const std::string& url,
                                        const std::string& bearer_token,
                                        const std::string& json_body,
                                        long timeout_seconds = 60) override {
        return http_post_json(url, bearer_token, json_body, timeout_seconds);
    }
};

} // namespace hecquin::ai
