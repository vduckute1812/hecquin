#pragma once

// Compile-time default paths injected by CMake (see
// `hecquin_set_runtime_defaults`).  The fallbacks below keep every
// executable compilable outside the project build *and* keep the four
// mains in sync — historically each main carried a slightly-different
// copy of this block (`.env/models/...` vs `.env/shared/models/...`).
//
// Include this single header instead of duplicating the block.  The
// macros are defined only when CMake hasn't already supplied them.

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
