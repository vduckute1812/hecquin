# pronunciation_drill CLI + fixture wiring.
#
# Requires: SQLite (for progress persistence), piper_speech (for reference TTS).
# If SQLite is absent the target is skipped so the rest of the build still
# succeeds — same pattern `english_tutor.cmake` uses.

if (NOT HECQUIN_HAS_SQLITE)
    message(STATUS "Skipping pronunciation_drill (SQLite3 not found).")
    return()
endif ()

if (NOT DEFINED HECQUIN_SOUND_SRC_ROOT)
    set(HECQUIN_SOUND_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif ()

set(DEFAULT_PRONUNCIATION_MODEL_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/.env/shared/models/pronunciation/wav2vec2_phoneme.onnx"
    CACHE STRING "Default wav2vec2 phoneme ONNX model path")
set(DEFAULT_PRONUNCIATION_VOCAB_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/.env/shared/models/pronunciation/vocab.json"
    CACHE STRING "Default phoneme vocab.json path")

add_executable(pronunciation_drill
    ${HECQUIN_SOUND_SRC_ROOT}/learning/cli/PronunciationDrillMain.cpp
)
target_link_libraries(pronunciation_drill PRIVATE
    hecquin_voice_pipeline
    hecquin_learning
    hecquin_drill
    hecquin_piper_speech)

hecquin_set_runtime_defaults(pronunciation_drill)
target_compile_definitions(pronunciation_drill PRIVATE
    DEFAULT_PRONUNCIATION_MODEL_PATH="${DEFAULT_PRONUNCIATION_MODEL_PATH}"
    DEFAULT_PRONUNCIATION_VOCAB_PATH="${DEFAULT_PRONUNCIATION_VOCAB_PATH}"
)

hecquin_adhoc_codesign(pronunciation_drill)
