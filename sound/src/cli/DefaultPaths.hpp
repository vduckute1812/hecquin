#pragma once

// Compile-time default paths injected by CMake `hecquin_set_runtime_defaults`.
// The `#ifndef` fallbacks keep this header buildable standalone and keep all
// mains in sync (avoids the old `.env/models` vs `.env/shared/models` drift).

#include "config/ConfigStore.hpp"

#ifndef DEFAULT_MODEL_PATH
#define DEFAULT_MODEL_PATH ".env/shared/models/ggml-base.bin"
#endif
#ifndef DEFAULT_PIPER_MODEL_PATH
#define DEFAULT_PIPER_MODEL_PATH ".env/shared/models/piper/en_US-lessac-medium.onnx"
#endif
#ifndef DEFAULT_CONFIG_PATH
#define DEFAULT_CONFIG_PATH ConfigStore::kDefaultPath
#endif
#ifndef DEFAULT_PROMPTS_DIR
#define DEFAULT_PROMPTS_DIR ""
#endif
#ifndef DEFAULT_PRONUNCIATION_MODEL_PATH
#define DEFAULT_PRONUNCIATION_MODEL_PATH ""
#endif
#ifndef DEFAULT_PRONUNCIATION_VOCAB_PATH
#define DEFAULT_PRONUNCIATION_VOCAB_PATH ""
#endif
