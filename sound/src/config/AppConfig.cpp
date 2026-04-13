#include "config/AppConfig.hpp"

AppConfig AppConfig::load(const char* env_file_path) {
    const ConfigStore store = ConfigStore::from_path(env_file_path);
    AppConfig cfg;
    cfg.ai = AiClientConfig::from_store(store);
    return cfg;
}
