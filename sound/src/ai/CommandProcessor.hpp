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
 * Local regex first, then `ChatClient`. Default ctor owns `CurlHttpClient`;
 * inject `IHttpClient&` for tests.
 *
 * Not thread-safe across concurrent `process`/`match_local` (shared `ChatClient`
 * state). `process` blocks on HTTP when chat runs; `VoiceListener` calls it sync.
 */
class CommandProcessor {
public:
    explicit CommandProcessor(AiClientConfig config = AiClientConfig::from_default_config(),
                              hecquin::ai::LocalIntentMatcherConfig matcher_cfg = {});

    /** Inject a specific HTTP client (e.g. a fake in tests). */
    CommandProcessor(AiClientConfig config,
                     hecquin::ai::IHttpClient& http,
                     hecquin::ai::LocalIntentMatcherConfig matcher_cfg = {});

    /** Local match, else chat (blocks on HTTP). */
    [[nodiscard]] Action process(const std::string& transcript);

    /** `std::async(process)`; keep `this` alive until the future is ready. */
    [[nodiscard]] std::future<Action> process_async(const std::string& transcript);

    /** Run only the local regex layer. Returns nullopt on no match. */
    [[nodiscard]] std::optional<Action> match_local(const std::string& transcript) const;

    const AiClientConfig& config() const { return config_; }

private:
    AiClientConfig                               config_;
    hecquin::ai::LocalIntentMatcher              matcher_;
    std::unique_ptr<hecquin::ai::CurlHttpClient> owned_http_;
    hecquin::ai::IHttpClient&                    http_;
    hecquin::ai::ChatClient                      chat_;
};
