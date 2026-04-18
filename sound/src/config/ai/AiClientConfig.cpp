#include "config/ai/AiClientConfig.hpp"

#include "config/ConfigStore.hpp"

#include <fstream>
#include <sstream>

AiClientConfig AiClientConfig::from_store(const ConfigStore& store) {
    AiClientConfig c;
    c.api_key = store.resolve("OPENAI_API_KEY");
    if (c.api_key.empty()) {
        c.api_key = store.resolve("HECQUIN_AI_API_KEY");
    }
    if (c.api_key.empty()) {
        c.api_key = store.resolve("GEMINI_API_KEY");
    }
    if (c.api_key.empty()) {
        c.api_key = store.resolve("GOOGLE_API_KEY");
    }
    std::string base = store.resolve("OPENAI_BASE_URL");
    if (base.empty()) {
        base = store.resolve("HECQUIN_AI_BASE_URL");
    }
    if (base.empty()) {
        base = "https://api.openai.com/v1";
    }
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    c.chat_completions_url = base + "/chat/completions";

    std::string model = store.resolve("HECQUIN_AI_MODEL");
    if (model.empty()) {
        model = store.resolve("OPENAI_MODEL");
    }
    if (!model.empty()) {
        c.model = std::move(model);
    }
    return c;
}

AiClientConfig AiClientConfig::from_default_config() {
    return from_store(ConfigStore::load_default());
}

std::string AiClientConfig::load_system_prompt(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

bool AiClientConfig::ready() const {
#ifdef HECQUIN_WITH_CURL
    return !api_key.empty();
#else
    (void)api_key;
    return false;
#endif
}
