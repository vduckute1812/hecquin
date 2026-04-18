#include "config/AppConfig.hpp"

AppConfig AppConfig::load(const char* env_file_path) {
    const ConfigStore store = ConfigStore::from_path(env_file_path);
    AppConfig cfg;
    cfg.ai = AiClientConfig::from_store(store);

    const std::string idx = store.resolve("AUDIO_DEVICE_INDEX");
    if (!idx.empty()) {
        cfg.audio.device_index = std::stoi(idx);
    }

    return cfg;
}
