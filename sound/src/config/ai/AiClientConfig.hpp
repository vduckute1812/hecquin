#pragma once

#include <string>

class ConfigStore;

/** OpenAI-compatible HTTP chat client settings (keys read via ConfigStore; see README for Gemini). */
struct AiClientConfig {
    std::string chat_completions_url;
    std::string api_key;
    std::string model = "gpt-4o-mini";

    static AiClientConfig from_store(const ConfigStore& store);
    /** Equivalent to `from_store(ConfigStore::load_default())` — one full read of default env file. */
    static AiClientConfig from_default_config();

    bool ready() const;
};
