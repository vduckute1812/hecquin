#include "config/AppConfig.hpp"

static constexpr const char* kDefaultSystemPrompt =
    "You are a voice assistant on a smart speaker. "
    "Reply in 1-3 short sentences using plain spoken language. "
    "No markdown, no bullet points, no lists, no special formatting.";

static constexpr const char* kDefaultTutorPrompt =
    "You are a friendly English tutor on a smart speaker. "
    "The user just said one sentence. Detect grammar errors and reply in spoken English only. "
    "Format: 1) You said: ... 2) Better: ... 3) one short reason. "
    "No markdown, no bullet points, no lists. "
    "Use the provided reference snippets only if they are relevant.";

AppConfig AppConfig::load(const char* env_file_path, const char* prompts_dir) {
    const ConfigStore store = ConfigStore::from_path(env_file_path);
    AppConfig cfg;
    cfg.ai = AiClientConfig::from_store(store);

    if (prompts_dir) {
        cfg.ai.system_prompt =
            AiClientConfig::load_system_prompt(std::string(prompts_dir) + "/system_prompt.txt");
        cfg.ai.tutor_system_prompt =
            AiClientConfig::load_system_prompt(std::string(prompts_dir) + "/english_tutor_prompt.txt");
    }
    if (cfg.ai.system_prompt.empty()) {
        cfg.ai.system_prompt = kDefaultSystemPrompt;
    }
    if (cfg.ai.tutor_system_prompt.empty()) {
        cfg.ai.tutor_system_prompt = kDefaultTutorPrompt;
    }

    const std::string idx = store.resolve("AUDIO_DEVICE_INDEX");
    if (!idx.empty()) {
        cfg.audio.device_index = std::stoi(idx);
    }

    const std::string db_path = store.resolve("HECQUIN_LEARNING_DB_PATH");
    if (!db_path.empty()) {
        cfg.learning.db_path = db_path;
    }
    const std::string curriculum = store.resolve("HECQUIN_LEARNING_CURRICULUM_DIR");
    if (!curriculum.empty()) {
        cfg.learning.curriculum_dir = curriculum;
    }
    const std::string custom = store.resolve("HECQUIN_LEARNING_CUSTOM_DIR");
    if (!custom.empty()) {
        cfg.learning.custom_dir = custom;
    }
    const std::string topk = store.resolve("HECQUIN_LEARNING_RAG_TOPK");
    if (!topk.empty()) {
        try {
            cfg.learning.rag_top_k = std::stoi(topk);
        } catch (...) {
        }
    }
    const std::string start_phrases = store.resolve("HECQUIN_LESSON_START_PHRASES");
    if (!start_phrases.empty()) {
        cfg.learning.lesson_start_phrases = start_phrases;
    }
    const std::string end_phrases = store.resolve("HECQUIN_LESSON_END_PHRASES");
    if (!end_phrases.empty()) {
        cfg.learning.lesson_end_phrases = end_phrases;
    }

    return cfg;
}
