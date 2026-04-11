#pragma once

#include "Action.hpp"

#include <future>
#include <optional>
#include <string>

struct AiClientConfig {
    /** OpenAI-compatible chat completions URL (e.g. https://api.openai.com/v1/chat/completions). */
    std::string chat_completions_url;
    std::string api_key;
    std::string model = "gpt-4o-mini";

    static AiClientConfig from_environment();
    bool ready() const;
};

/**
 * Routes speech transcripts to local commands, interaction modes, or an HTTP chat API.
 */
class CommandProcessor {
public:
    explicit CommandProcessor(AiClientConfig config = AiClientConfig::from_environment());

    /** Full pipeline: fast local regex; external API runs on a worker thread (caller waits on result). */
    Action process(const std::string& transcript);

    /** Same as process but caller can poll/wait without blocking until .wait() / .get(). */
    std::future<Action> process_async(const std::string& transcript);

    const AiClientConfig& config() const { return config_; }

private:
    AiClientConfig config_;
    std::optional<Action> match_local_(const std::string& normalized) const;
    Action call_external_api_(const std::string& user_text) const;
};
