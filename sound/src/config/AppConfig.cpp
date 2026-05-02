#include "config/AppConfig.hpp"

#include "common/PathUtils.hpp"

#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <string>

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

namespace {

/** Assign `store.resolve(key)` to `dst` only when the env value is
 *  non-empty.  Used everywhere the config field is a plain string. */
void resolve_string(const ConfigStore& store, const char* key, std::string& dst) {
    const std::string v = store.resolve(key);
    if (!v.empty()) dst = v;
}

/** Same shape, but parses the env value as an integer; logs a single
 *  `[env] ignoring invalid …` line on a parse failure and leaves `dst`
 *  untouched.  Mirrors the warning style used elsewhere in the codebase. */
void resolve_int(const ConfigStore& store, const char* key, int& dst) {
    const std::string v = store.resolve(key);
    if (v.empty()) return;
    try {
        dst = std::stoi(v);
    } catch (...) {
        std::cerr << "[env] ignoring invalid " << key << "=" << v
                  << std::endl;
    }
}

/**
 * One row in the env-key→string-field table.  Members marked
 * `is_path = true` are anchored against the config-file directory at
 * the end of `load()` so relative paths work regardless of CWD.
 */
struct StringBinding {
    const char* key;
    std::string* dst;
    bool is_path = false;
};

} // namespace

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

    // Audio device — a parse-failure used to abort startup with an
    // uncaught `std::out_of_range`; resolve_int now warns + falls back
    // to the default index.
    resolve_int(store, "AUDIO_DEVICE_INDEX", cfg.audio.device_index);

    // String / path fields, table-driven so future additions don't drift
    // between the resolve loop and the anchor loop below.
    const StringBinding bindings[] = {
        { "HECQUIN_LEARNING_DB_PATH",         &cfg.learning.db_path,                true  },
        { "HECQUIN_LEARNING_CURRICULUM_DIR",  &cfg.learning.curriculum_dir,         true  },
        { "HECQUIN_LEARNING_CUSTOM_DIR",      &cfg.learning.custom_dir,             true  },
        { "HECQUIN_LESSON_START_PHRASES",     &cfg.learning.lesson_start_phrases,   false },
        { "HECQUIN_LESSON_END_PHRASES",       &cfg.learning.lesson_end_phrases,     false },
        { "HECQUIN_DRILL_START_PHRASES",      &cfg.learning.drill_start_phrases,    false },
        { "HECQUIN_DRILL_END_PHRASES",        &cfg.learning.drill_end_phrases,      false },
        { "HECQUIN_INGEST_API_LOG_CSV",       &cfg.learning.ingest_api_log_csv,     true  },
        { "HECQUIN_INGEST_RUN_SUMMARY_CSV",   &cfg.learning.ingest_run_summary_csv, true  },
        { "HECQUIN_PRONUNCIATION_MODEL",      &cfg.pronunciation.model_path,        true  },
        { "HECQUIN_PRONUNCIATION_VOCAB",      &cfg.pronunciation.vocab_path,        true  },
        { "HECQUIN_ONNX_PROVIDER",            &cfg.pronunciation.onnx_provider,     false },
        { "HECQUIN_DRILL_SENTENCES",          &cfg.pronunciation.drill_sentences_path, true },
        { "HECQUIN_PRONUNCIATION_CALIBRATION", &cfg.pronunciation.calibration_path, true  },
        { "HECQUIN_LOCALE",                   &cfg.locale.ui,                       false },
        { "HECQUIN_WHISPER_LANGUAGE",         &cfg.locale.whisper_language,         false },
        { "HECQUIN_ESPEAK_VOICE",             &cfg.locale.espeak_voice,             false },
        { "HECQUIN_MUSIC_PROVIDER",           &cfg.music.provider,                  false },
        { "HECQUIN_YT_COOKIES_FILE",          &cfg.music.cookies_file,              true  },
        { "HECQUIN_YT_DLP_BIN",               &cfg.music.yt_dlp_binary,             false },
        { "HECQUIN_FFMPEG_BIN",               &cfg.music.ffmpeg_binary,             false },
    };
    for (const auto& b : bindings) {
        resolve_string(store, b.key, *b.dst);
    }

    resolve_int(store, "HECQUIN_LEARNING_RAG_TOPK",     cfg.learning.rag_top_k);
    resolve_int(store, "HECQUIN_DRILL_PASS_THRESHOLD",  cfg.learning.drill_pass_threshold);
    resolve_int(store, "HECQUIN_MUSIC_SAMPLE_RATE",     cfg.music.sample_rate_hz);

    // Anchor relative paths against the config file's dir (cwd-independent).
    std::string base_dir;
    if (env_file_path && *env_file_path) {
        base_dir = std::filesystem::path(env_file_path).parent_path().string();
    }
    auto anchor = [&](std::string& s) {
        s = hecquin::common::resolve_against_dir(base_dir, std::move(s));
    };
    for (const auto& b : bindings) {
        if (b.is_path) anchor(*b.dst);
    }

    return cfg;
}
