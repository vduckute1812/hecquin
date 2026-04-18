#pragma once

#include "config/ai/AiClientConfig.hpp"
#include "config/ConfigStore.hpp"
#include "voice/AudioCapture.hpp"

/**
 * Top-level application settings loaded from `.env/config.env` (and environment overrides).
 * Add new sections here as the program grows (audio, logging, feature flags, etc.).
 */
struct AppConfig {
    AiClientConfig ai;
    AudioCaptureConfig audio;

    static AppConfig load(const char* env_file_path = ConfigStore::kDefaultPath);
};
