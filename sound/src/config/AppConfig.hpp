#pragma once

#include "config/ai/AiClientConfig.hpp"
#include "config/ConfigStore.hpp"
#include "voice/AudioCaptureConfig.hpp"

#include <string>

/** Settings for the English-tutor / learning subsystem. */
struct LearningConfig {
    std::string db_path = ".env/shared/learning/db/learning.sqlite";
    std::string curriculum_dir = ".env/shared/learning/curriculum";
    std::string custom_dir = ".env/shared/learning/custom";
    int rag_top_k = 3;
    std::string lesson_start_phrases = "start english lesson|begin lesson|start lesson";
    std::string lesson_end_phrases = "exit lesson|end lesson|stop lesson";
    std::string drill_start_phrases = "start pronunciation drill|begin pronunciation|start drill";
    std::string drill_end_phrases = "exit drill|end drill|stop drill";
    int drill_pass_threshold = 75;
};

/** Settings for the pronunciation / intonation drill subsystem. */
struct PronunciationConfig {
    std::string model_path = ".env/shared/models/pronunciation/wav2vec2_phoneme.onnx";
    std::string vocab_path = ".env/shared/models/pronunciation/vocab.json";
    std::string onnx_provider = "cpu";
    /** Optional file with one drill sentence per line. */
    std::string drill_sentences_path = ".env/shared/learning/drill/sentences.txt";
    /**
     * Optional JSON file with per-phoneme `min_logp`/`max_logp` overrides
     * (see `PronunciationScorerConfig::load_calibration_json`).  Missing
     * file is silently ignored — scorer falls back to global anchors.
     */
    std::string calibration_path = ".env/shared/models/pronunciation/calibration.json";
};

/**
 * Top-level application settings loaded from `.env/config.env` (and environment overrides).
 * Add new sections here as the program grows (audio, logging, feature flags, etc.).
 */
/**
 * Locale settings.  Every subsystem consumes a subset — WhisperEngine wants
 * a 2-letter ISO code (`en`, `es`), G2P wants an espeak voice (`en-us`,
 * `es`), the chat prompt selector uses the `ui` code for prompt lookup.
 * Defaults keep the current English-only behaviour bit-for-bit; setting any
 * of these to empty falls back to the subsystem's own default.
 */
struct LocaleConfig {
    /** Display / prompt-lookup locale, e.g. "en-US", "fr-FR". */
    std::string ui = "en-US";
    /** Whisper transcription language, 2-letter ISO. "" = auto-detect. */
    std::string whisper_language = "en";
    /** espeak-ng voice for G2P, e.g. "en-us". "" = use G2P default. */
    std::string espeak_voice = "en-us";
};

struct AppConfig {
    AiClientConfig ai;
    AudioCaptureConfig audio;
    LearningConfig learning;
    PronunciationConfig pronunciation;
    LocaleConfig locale;

    static AppConfig load(const char* env_file_path = ConfigStore::kDefaultPath,
                          const char* prompts_dir = nullptr);
};
