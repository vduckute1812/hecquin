#pragma once

#include "actions/Action.hpp"
#include "config/ai/AiClientConfig.hpp"

#include <atomic>
#include <chrono>
#include <string>

namespace hecquin::ai {

class IHttpClient;

/**
 * Sends a single-turn user message to an OpenAI-compatible `/chat/completions`
 * endpoint and wraps the assistant reply in an `ExternalApiAction`.
 *
 * Accepts any `IHttpClient` so tests can inject canned HTTP responses.
 *
 * Tier-3 #13: tracks consecutive transport / 5xx failures.  After
 * `failure_threshold` failures within `failure_window`, the client
 * enters a cooldown for `cooldown_duration` and answers requests with
 * a canned spoken reply instead of attempting the HTTP call (which
 * was about to time out anyway).  Successful replies reset the state.
 * Tunable via `HECQUIN_CHAT_COOLDOWN_*` env vars (see `apply_env_overrides`).
 */
class ChatClient {
public:
    using Clock = std::chrono::steady_clock;

    ChatClient(AiClientConfig config, IHttpClient& http);

    Action ask(const std::string& user_text);

    /** Build the `/chat/completions` request JSON for `user_text`. Exposed for tests. */
    std::string build_request_body(const std::string& user_text) const;

    bool in_cooldown(Clock::time_point now = Clock::now()) const;

    /** Apply `HECQUIN_CHAT_COOLDOWN_FAILURES` /
     *  `HECQUIN_CHAT_COOLDOWN_WINDOW_MS` /
     *  `HECQUIN_CHAT_COOLDOWN_DURATION_MS`. */
    void apply_env_overrides();

private:
    void note_failure_(Clock::time_point now);
    void note_success_();

    AiClientConfig config_;
    IHttpClient&   http_;

    int failure_threshold_ = 2;
    std::chrono::milliseconds failure_window_{30000};
    std::chrono::milliseconds cooldown_duration_{60000};

    int               consecutive_failures_ = 0;
    Clock::time_point first_failure_at_{};
    Clock::time_point cooldown_until_{};
};

} // namespace hecquin::ai
