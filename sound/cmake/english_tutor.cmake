# English-tutor CLI + ingest CLI targets.
# These require SQLite3 (and ideally sqlite-vec). If SQLite is missing they are skipped.

if (NOT HECQUIN_HAS_SQLITE)
    message(STATUS "Skipping english_tutor / english_ingest (SQLite3 not found).")
    return()
endif ()

if (NOT DEFINED HECQUIN_SOUND_SRC_ROOT)
    set(HECQUIN_SOUND_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif ()

# english_ingest: console-only ingest tool (no SDL2 / Whisper / Piper).
add_executable(english_ingest
    ${HECQUIN_SOUND_SRC_ROOT}/learning/cli/EnglishIngest.cpp
)
target_link_libraries(english_ingest PRIVATE hecquin_learning)
hecquin_set_runtime_defaults(english_ingest)

# english_tutor: full voice pipeline in lesson mode.
add_executable(english_tutor
    ${HECQUIN_SOUND_SRC_ROOT}/learning/cli/EnglishTutorMain.cpp
)
target_link_libraries(english_tutor PRIVATE
    hecquin_voice_pipeline
    hecquin_learning
    hecquin_piper_speech)
hecquin_set_runtime_defaults(english_tutor)

hecquin_adhoc_codesign(english_ingest english_tutor)
