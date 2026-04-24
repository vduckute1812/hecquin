#pragma once

#include "actions/Action.hpp"
#include "ai/ChatClient.hpp"
#include "ai/IHttpClient.hpp"
#include "ai/LocalIntentMatcher.hpp"
#include "config/ai/AiClientConfig.hpp"

#include <future>
#include <memory>
#include <optional>
#include <string>

/**
 * High-level router: tries the fast regex matcher first, falls back to the
 * HTTP chat client.  Composes `LocalIntentMatcher` + `ChatClient` so each is
 * independently testable.
 *
 * The default constructor builds its own `CurlHttpClient`; tests (or
 * alternative transports) can inject an `IHttpClient&` instead.
 */
class CommandProcessor {
public:
    explicit CommandProcessor(AiClientConfig config = AiClientConfig::from_default_config(),
                              hecquin::ai::LocalIntentMatcherConfig matcher_cfg = {});

    /** Inject a specific HTTP client (e.g. a fake in tests). */
    CommandProcessor(AiClientConfig config,
                     hecquin::ai::IHttpClient& http,
                     hecquin::ai::LocalIntentMatcherConfig matcher_cfg = {});

    /** Full pipeline: fast local regex, then external chat API. */
    Action process(const std::string& transcript);

    /** Run `process` on a worker thread. Caller must outlive the returned future. */
    std::future<Action> process_async(const std::string& transcript);

    /** Run only the local regex layer. Returns nullopt on no match. */
    std::optional<Action> match_local(const std::string& transcript) const;

    const AiClientConfig& config() const { return config_; }

private:
    AiClientConfig                               config_;
    hecquin::ai::LocalIntentMatcher              matcher_;
    std::unique_ptr<hecquin::ai::CurlHttpClient> owned_http_;
    hecquin::ai::IHttpClient&                    http_;
    hecquin::ai::ChatClient                      chat_;
};
