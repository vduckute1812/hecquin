#pragma once

#include "config/ai/AiClientConfig.hpp"
#include "config/ConfigStore.hpp"
#include "voice/AudioCapture.hpp"

#include <string>

/** Settings for the English-tutor / learning subsystem. */
struct LearningConfig {
    std::string db_path = ".env/shared/learning/db/learning.sqlite";
    std::string curriculum_dir = ".env/shared/learning/curriculum";
    std::string custom_dir = ".env/shared/learning/custom";
    int rag_top_k = 3;
    std::string lesson_start_phrases = "start english lesson|begin lesson|start lesson";
    std::string lesson_end_phrases = "exit lesson|end lesson|stop lesson";
};

/**
 * Top-level application settings loaded from `.env/config.env` (and environment overrides).
 * Add new sections here as the program grows (audio, logging, feature flags, etc.).
 */
struct AppConfig {
    AiClientConfig ai;
    AudioCaptureConfig audio;
    LearningConfig learning;

    static AppConfig load(const char* env_file_path = ConfigStore::kDefaultPath,
                          const char* prompts_dir = nullptr);
};
