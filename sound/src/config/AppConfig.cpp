#include "config/AppConfig.hpp"

static constexpr const char* kDefaultSystemPrompt =
    "You are a voice assistant on a smart speaker. "
    "Reply in 1-3 short sentences using plain spoken language. "
    "No markdown, no bullet points, no lists, no special formatting.";

AppConfig AppConfig::load(const char* env_file_path, const char* prompts_dir) {
    const ConfigStore store = ConfigStore::from_path(env_file_path);
    AppConfig cfg;
    cfg.ai = AiClientConfig::from_store(store);

    if (prompts_dir) {
        cfg.ai.system_prompt =
            AiClientConfig::load_system_prompt(std::string(prompts_dir) + "/system_prompt.txt");
    }
    if (cfg.ai.system_prompt.empty()) {
        cfg.ai.system_prompt = kDefaultSystemPrompt;
    }

    const std::string idx = store.resolve("AUDIO_DEVICE_INDEX");
    if (!idx.empty()) {
        cfg.audio.device_index = std::stoi(idx);
    }

    return cfg;
}
