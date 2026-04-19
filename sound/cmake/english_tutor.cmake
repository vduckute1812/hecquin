# English-tutor CLI + ingest CLI targets.
# These require SQLite3 (and ideally sqlite-vec). If SQLite is missing they are skipped.

if (NOT HECQUIN_HAS_SQLITE)
    message(STATUS "Skipping english_tutor / english_ingest (SQLite3 not found).")
    return()
endif ()

if (NOT DEFINED HECQUIN_SOUND_SRC_ROOT)
    set(HECQUIN_SOUND_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif ()

set(HECQUIN_LEARNING_SOURCES
    ${HECQUIN_SOUND_SRC_ROOT}/learning/LearningStore.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/learning/EmbeddingClient.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/learning/Ingestor.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/learning/RetrievalService.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/learning/ProgressTracker.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/learning/EnglishTutorProcessor.cpp
)

# english_ingest: console-only ingest tool (no SDL2 / Whisper / Piper).
add_executable(english_ingest
    ${HECQUIN_SOUND_SRC_ROOT}/learning/cli/EnglishIngest.cpp
    ${HECQUIN_LEARNING_SOURCES}
    ${HECQUIN_SOUND_SRC_ROOT}/config/ConfigStore.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/config/AppConfig.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/config/ai/AiClientConfig.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/ai/HttpClient.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/ai/OpenAiChatContent.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/voice/AudioCapture.cpp
)

target_include_directories(english_ingest PRIVATE ${HECQUIN_SOUND_SRC_ROOT})

target_link_libraries(english_ingest PRIVATE
    hecquin_deps_sqlite_vec
    hecquin_deps_curl
    hecquin_deps_sdl2
)

target_compile_definitions(english_ingest PRIVATE
    DEFAULT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/.env/config.env"
    DEFAULT_PROMPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.env/prompts"
)

# english_tutor: full voice pipeline in lesson mode.
add_executable(english_tutor
    ${HECQUIN_SOUND_SRC_ROOT}/learning/cli/EnglishTutorMain.cpp
    ${HECQUIN_LEARNING_SOURCES}
    ${HECQUIN_SOUND_SRC_ROOT}/voice/AudioCapture.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/voice/WhisperEngine.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/voice/VoiceListener.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/config/ConfigStore.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/config/AppConfig.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/config/ai/AiClientConfig.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/ai/OpenAiChatContent.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/ai/HttpClient.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/ai/CommandProcessor.cpp
)

target_include_directories(english_tutor PRIVATE ${HECQUIN_SOUND_SRC_ROOT})

target_link_libraries(english_tutor PRIVATE
    hecquin_deps_sqlite_vec
    hecquin_deps_whisper
    hecquin_deps_sdl2
    hecquin_deps_curl
    hecquin_piper_speech
)

target_compile_definitions(english_tutor PRIVATE
    DEFAULT_MODEL_PATH="${MODELS_DIR}/ggml-base.bin"
    DEFAULT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/.env/config.env"
    DEFAULT_PROMPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.env/prompts"
)

hecquin_adhoc_codesign(english_ingest english_tutor)
