#pragma once

#include "actions/Action.hpp"
#include "config/ai/AiClientConfig.hpp"

#include <string>

namespace hecquin::ai {

class IHttpClient;

/**
 * Sends a single-turn user message to an OpenAI-compatible `/chat/completions`
 * endpoint and wraps the assistant reply in an `ExternalApiAction`.
 *
 * Accepts any `IHttpClient` so tests can inject canned HTTP responses.
 */
class ChatClient {
public:
    ChatClient(AiClientConfig config, IHttpClient& http);

    Action ask(const std::string& user_text) const;

    /** Build the `/chat/completions` request JSON for `user_text`. Exposed for tests. */
    std::string build_request_body(const std::string& user_text) const;

private:
    AiClientConfig config_;
    IHttpClient&   http_;
};

} // namespace hecquin::ai
