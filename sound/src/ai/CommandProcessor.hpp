#pragma once

#include "actions/Action.hpp"
#include "config/ai/AiClientConfig.hpp"

#include <future>
#include <optional>
#include <string>

/**
 * Routes speech transcripts to local commands, interaction modes, or an HTTP chat API.
 */
class CommandProcessor {
public:
    explicit CommandProcessor(AiClientConfig config = AiClientConfig::from_default_config());

    /** Full pipeline: fast local regex; external API runs on a worker thread (caller waits on result). */
    Action process(const std::string& transcript);

    /** Same as process but caller can poll/wait without blocking until .wait() / .get(). */
    std::future<Action> process_async(const std::string& transcript);

    const AiClientConfig& config() const { return config_; }

private:
    AiClientConfig config_;
    std::optional<Action> match_local_(const std::string& normalized) const;
    std::string build_chat_body_(const std::string& user_text) const;
    Action call_external_api_(const std::string& user_text) const;
};
