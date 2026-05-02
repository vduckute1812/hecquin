// Verifies the documented precedence order in `AiClientConfig::from_store`:
//
//   API key:    OPENAI_API_KEY > HECQUIN_AI_API_KEY > GEMINI_API_KEY > GOOGLE_API_KEY
//   Base URL:   OPENAI_BASE_URL > HECQUIN_AI_BASE_URL > default
//   Model:      HECQUIN_AI_MODEL > OPENAI_MODEL > built-in default
//   Embedding:  HECQUIN_AI_EMBEDDING_MODEL > OPENAI_EMBEDDING_MODEL > default
//
// `ConfigStore::resolve` reads the process environment first, then the
// .env file.  We exercise both layers with a temp file for the .env
// path and `setenv` for the process-environment overrides.

#include "config/ConfigStore.hpp"
#include "config/ai/AiClientConfig.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

int fail(const char* message) {
    std::cerr << "[test_ai_client_config_precedence] FAIL: " << message << std::endl;
    return 1;
}

std::string make_tmp_env(const std::string& contents) {
    char buf[] = "/tmp/hecquin_aicfg_XXXXXX";
    const int fd = mkstemp(buf);
    if (fd < 0) return {};
    close(fd);
    std::ofstream out(buf);
    out << contents;
    return std::string(buf);
}

void clear_env() {
    for (const char* k : {
            "OPENAI_API_KEY", "HECQUIN_AI_API_KEY",
            "GEMINI_API_KEY", "GOOGLE_API_KEY",
            "OPENAI_BASE_URL", "HECQUIN_AI_BASE_URL",
            "HECQUIN_AI_MODEL", "OPENAI_MODEL",
            "HECQUIN_AI_EMBEDDING_MODEL", "OPENAI_EMBEDDING_MODEL",
            "HECQUIN_AI_EMBEDDING_DIM",
        }) {
        ::unsetenv(k);
    }
}

} // namespace

int main() {
    clear_env();

    // 1. Empty store → empty fields, defaults preserved.
    {
        const auto path = make_tmp_env("");
        const auto store = ConfigStore::from_path(path.c_str());
        const auto cfg = AiClientConfig::from_store(store);
        std::remove(path.c_str());
        if (!cfg.api_key.empty())
            return fail("empty store should yield empty api_key");
        if (cfg.chat_completions_url != "https://api.openai.com/v1/chat/completions")
            return fail("default base URL should be openai.com");
        if (cfg.embeddings_url != "https://api.openai.com/v1/embeddings")
            return fail("default embeddings url should be openai.com");
        if (cfg.model != "gpt-4o-mini")
            return fail("default model should be gpt-4o-mini");
    }

    // 2. API key precedence: OPENAI_API_KEY beats every other key.
    {
        const auto path = make_tmp_env(
            "OPENAI_API_KEY=from_openai\n"
            "HECQUIN_AI_API_KEY=from_hecquin\n"
            "GEMINI_API_KEY=from_gemini\n"
            "GOOGLE_API_KEY=from_google\n");
        const auto store = ConfigStore::from_path(path.c_str());
        const auto cfg = AiClientConfig::from_store(store);
        std::remove(path.c_str());
        if (cfg.api_key != "from_openai")
            return fail("OPENAI_API_KEY must win when present");
    }

    // 3. API key precedence: GEMINI_API_KEY wins when only Hecquin/Google
    //    are set alongside it.
    {
        const auto path = make_tmp_env(
            "HECQUIN_AI_API_KEY=from_hecquin\n"
            "GEMINI_API_KEY=from_gemini\n"
            "GOOGLE_API_KEY=from_google\n");
        const auto store = ConfigStore::from_path(path.c_str());
        const auto cfg = AiClientConfig::from_store(store);
        std::remove(path.c_str());
        if (cfg.api_key != "from_hecquin")
            return fail("HECQUIN_AI_API_KEY should beat GEMINI/GOOGLE");
    }

    // 4. API key precedence: GOOGLE_API_KEY is the last resort.
    {
        const auto path = make_tmp_env("GOOGLE_API_KEY=from_google\n");
        const auto store = ConfigStore::from_path(path.c_str());
        const auto cfg = AiClientConfig::from_store(store);
        std::remove(path.c_str());
        if (cfg.api_key != "from_google")
            return fail("GOOGLE_API_KEY should be honoured when alone");
    }

    // 5. Base URL precedence: HECQUIN_AI_BASE_URL beats the default,
    //    OPENAI_BASE_URL beats both.  Trailing slashes are stripped.
    {
        const auto path = make_tmp_env(
            "HECQUIN_AI_BASE_URL=https://hq.example/v1//\n");
        const auto store = ConfigStore::from_path(path.c_str());
        const auto cfg = AiClientConfig::from_store(store);
        std::remove(path.c_str());
        if (cfg.chat_completions_url != "https://hq.example/v1/chat/completions")
            return fail("HECQUIN_AI_BASE_URL must win over default and "
                        "trailing slashes must strip");
    }
    {
        const auto path = make_tmp_env(
            "OPENAI_BASE_URL=https://oai.example/v2\n"
            "HECQUIN_AI_BASE_URL=https://hq.example/v1\n");
        const auto store = ConfigStore::from_path(path.c_str());
        const auto cfg = AiClientConfig::from_store(store);
        std::remove(path.c_str());
        if (cfg.chat_completions_url != "https://oai.example/v2/chat/completions")
            return fail("OPENAI_BASE_URL should beat HECQUIN_AI_BASE_URL");
    }

    // 6. Model precedence: HECQUIN_AI_MODEL > OPENAI_MODEL > default.
    {
        const auto path = make_tmp_env(
            "HECQUIN_AI_MODEL=hq-flash\n"
            "OPENAI_MODEL=oai-mini\n");
        const auto store = ConfigStore::from_path(path.c_str());
        const auto cfg = AiClientConfig::from_store(store);
        std::remove(path.c_str());
        if (cfg.model != "hq-flash")
            return fail("HECQUIN_AI_MODEL must win over OPENAI_MODEL");
    }
    {
        const auto path = make_tmp_env("OPENAI_MODEL=oai-mini\n");
        const auto store = ConfigStore::from_path(path.c_str());
        const auto cfg = AiClientConfig::from_store(store);
        std::remove(path.c_str());
        if (cfg.model != "oai-mini")
            return fail("OPENAI_MODEL should be honoured when alone");
    }

    // 7. Embedding model precedence + dim parse.
    {
        const auto path = make_tmp_env(
            "HECQUIN_AI_EMBEDDING_MODEL=text-embedding-3-large\n"
            "HECQUIN_AI_EMBEDDING_DIM=3072\n");
        const auto store = ConfigStore::from_path(path.c_str());
        const auto cfg = AiClientConfig::from_store(store);
        std::remove(path.c_str());
        if (cfg.embedding_model != "text-embedding-3-large")
            return fail("HECQUIN_AI_EMBEDDING_MODEL should be honoured");
        if (cfg.embedding_dim != 3072)
            return fail("HECQUIN_AI_EMBEDDING_DIM should be parsed");
    }

    // 8. Process env beats .env file (ConfigStore::resolve contract).
    {
        const auto path = make_tmp_env("OPENAI_API_KEY=from_file\n");
        ::setenv("OPENAI_API_KEY", "from_env", 1);
        const auto store = ConfigStore::from_path(path.c_str());
        const auto cfg = AiClientConfig::from_store(store);
        ::unsetenv("OPENAI_API_KEY");
        std::remove(path.c_str());
        if (cfg.api_key != "from_env")
            return fail("process env must override .env file");
    }

    std::cout << "[test_ai_client_config_precedence] OK" << std::endl;
    return 0;
}
