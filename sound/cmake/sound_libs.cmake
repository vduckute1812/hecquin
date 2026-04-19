# Internal static libraries for the sound module.
#
# Splits the tree into four narrow libs so the voice_detector / english_ingest /
# english_tutor executables just list their entry-point source + the libs they
# need, instead of re-listing ~10 files each.
#
# Dependency graph (dotted = header-only / deps_* interface targets):
#
#                    hecquin_config
#                          ▲
#                          │
#                    hecquin_ai ···· deps_curl, deps_json
#                       ▲       ▲
#                       │       │
#       hecquin_voice_pipeline   hecquin_learning ···· deps_sqlite_vec
#       (SDL2, Whisper)           (SQLite3, vec0)

if (NOT DEFINED HECQUIN_SOUND_SRC_ROOT)
    set(HECQUIN_SOUND_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif ()

# ---- hecquin_config ----------------------------------------------------------
if (NOT TARGET hecquin_config)
    add_library(hecquin_config STATIC
        ${HECQUIN_SOUND_SRC_ROOT}/config/ConfigStore.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/config/AppConfig.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/config/ai/AiClientConfig.cpp
    )
    target_include_directories(hecquin_config PUBLIC ${HECQUIN_SOUND_SRC_ROOT})
    # AiClientConfig::ready() is guarded by HECQUIN_WITH_CURL; link the curl
    # interface target so the compile def reaches this TU even though the
    # config lib itself doesn't call into libcurl.
    target_link_libraries(hecquin_config PUBLIC hecquin_deps_curl)
    set_target_properties(hecquin_config PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

# ---- hecquin_ai --------------------------------------------------------------
# HTTP transport + OpenAI-compat chat parser.  No voice / whisper deps so the
# learning lib can link this without dragging SDL2 in.
if (NOT TARGET hecquin_ai)
    add_library(hecquin_ai STATIC
        ${HECQUIN_SOUND_SRC_ROOT}/ai/HttpClient.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/ai/OpenAiChatContent.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/ai/LocalIntentMatcher.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/ai/ChatClient.cpp
    )
    target_include_directories(hecquin_ai PUBLIC ${HECQUIN_SOUND_SRC_ROOT})
    target_link_libraries(hecquin_ai PUBLIC
        hecquin_config
        hecquin_deps_curl
        hecquin_deps_json)
    set_target_properties(hecquin_ai PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

# ---- hecquin_voice_pipeline --------------------------------------------------
# Microphone capture → whisper.cpp → VoiceListener → CommandProcessor.
if (NOT TARGET hecquin_voice_pipeline)
    add_library(hecquin_voice_pipeline STATIC
        ${HECQUIN_SOUND_SRC_ROOT}/voice/AudioCapture.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/voice/WhisperEngine.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/voice/VoiceListener.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/voice/VoiceApp.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/ai/CommandProcessor.cpp
    )
    target_include_directories(hecquin_voice_pipeline PUBLIC ${HECQUIN_SOUND_SRC_ROOT})
    target_link_libraries(hecquin_voice_pipeline PUBLIC
        hecquin_ai
        hecquin_deps_whisper
        hecquin_deps_sdl2)
    set_target_properties(hecquin_voice_pipeline PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

# ---- hecquin_learning --------------------------------------------------------
# Vector store, ingestion, RAG retrieval, english tutor processor.
if (HECQUIN_HAS_SQLITE AND NOT TARGET hecquin_learning)
    add_library(hecquin_learning STATIC
        ${HECQUIN_SOUND_SRC_ROOT}/learning/LearningStore.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/learning/EmbeddingClient.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/learning/Ingestor.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/learning/TextChunker.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/learning/RetrievalService.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/learning/ProgressTracker.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/learning/EnglishTutorProcessor.cpp
    )
    target_include_directories(hecquin_learning PUBLIC ${HECQUIN_SOUND_SRC_ROOT})
    target_link_libraries(hecquin_learning PUBLIC
        hecquin_ai
        hecquin_deps_sqlite_vec)
    set_target_properties(hecquin_learning PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

# ---- Helper: set runtime-path compile defs ----------------------------------
# Adds DEFAULT_MODEL_PATH / DEFAULT_CONFIG_PATH / DEFAULT_PROMPTS_DIR to a
# target in one call — replaces the per-executable copy-pastes.
function(hecquin_set_runtime_defaults target)
    target_compile_definitions(${target} PRIVATE
        DEFAULT_MODEL_PATH="${MODELS_DIR}/ggml-base.bin"
        DEFAULT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/.env/config.env"
        DEFAULT_PROMPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.env/prompts"
    )
endfunction()
