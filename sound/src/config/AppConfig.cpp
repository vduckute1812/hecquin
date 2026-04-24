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
    const std::string drill_start = store.resolve("HECQUIN_DRILL_START_PHRASES");
    if (!drill_start.empty()) {
        cfg.learning.drill_start_phrases = drill_start;
    }
    const std::string drill_end = store.resolve("HECQUIN_DRILL_END_PHRASES");
    if (!drill_end.empty()) {
        cfg.learning.drill_end_phrases = drill_end;
    }
    const std::string drill_pass = store.resolve("HECQUIN_DRILL_PASS_THRESHOLD");
    if (!drill_pass.empty()) {
        try {
            cfg.learning.drill_pass_threshold = std::stoi(drill_pass);
        } catch (...) {
        }
    }

    const std::string pron_model = store.resolve("HECQUIN_PRONUNCIATION_MODEL");
    if (!pron_model.empty()) {
        cfg.pronunciation.model_path = pron_model;
    }
    const std::string pron_vocab = store.resolve("HECQUIN_PRONUNCIATION_VOCAB");
    if (!pron_vocab.empty()) {
        cfg.pronunciation.vocab_path = pron_vocab;
    }
    const std::string pron_provider = store.resolve("HECQUIN_ONNX_PROVIDER");
    if (!pron_provider.empty()) {
        cfg.pronunciation.onnx_provider = pron_provider;
    }
    const std::string drill_sentences = store.resolve("HECQUIN_DRILL_SENTENCES");
    if (!drill_sentences.empty()) {
        cfg.pronunciation.drill_sentences_path = drill_sentences;
    }
    const std::string calibration = store.resolve("HECQUIN_PRONUNCIATION_CALIBRATION");
    if (!calibration.empty()) {
        cfg.pronunciation.calibration_path = calibration;
    }

    const std::string ui_locale = store.resolve("HECQUIN_LOCALE");
    if (!ui_locale.empty()) cfg.locale.ui = ui_locale;
    const std::string whisper_lang = store.resolve("HECQUIN_WHISPER_LANGUAGE");
    if (!whisper_lang.empty()) cfg.locale.whisper_language = whisper_lang;
    const std::string espeak_voice = store.resolve("HECQUIN_ESPEAK_VOICE");
    if (!espeak_voice.empty()) cfg.locale.espeak_voice = espeak_voice;

    return cfg;
}
