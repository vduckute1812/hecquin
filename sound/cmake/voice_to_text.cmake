# Voice-detector executable (voice → text → command / AI routing).
#
# All compilation units live in `hecquin_voice_pipeline` — this file only
# declares the entry point and links the shared lib + piper TTS.

if (NOT DEFINED HECQUIN_SOUND_SRC_ROOT)
    set(HECQUIN_SOUND_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif ()

add_executable(voice_detector
    ${HECQUIN_SOUND_SRC_ROOT}/voice/VoiceDetector.cpp
)

target_link_libraries(voice_detector PRIVATE
    hecquin_voice_pipeline
    hecquin_piper_speech)

hecquin_set_runtime_defaults(voice_detector)
hecquin_adhoc_codesign(voice_detector)
