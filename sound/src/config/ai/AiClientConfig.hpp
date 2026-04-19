#pragma once

#include <string>

class ConfigStore;

/** OpenAI-compatible HTTP chat client settings (keys read via ConfigStore; see README for Gemini). */
struct AiClientConfig {
    std::string chat_completions_url;
    std::string embeddings_url;
    std::string api_key;
    std::string model = "gpt-4o-mini";
    std::string embedding_model = "gemini-embedding-001";
    int embedding_dim = 768;
    std::string system_prompt;
    std::string tutor_system_prompt;

    static AiClientConfig from_store(const ConfigStore& store);
    /** Equivalent to `from_store(ConfigStore::load_default())` — one full read of default env file. */
    static AiClientConfig from_default_config();

    /** Load a prompt from a text file; returns empty string on failure. */
    static std::string load_system_prompt(const std::string& path);

    bool ready() const;
};
