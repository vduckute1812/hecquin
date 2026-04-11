# Shared Piper + SDL playback (used by voice_detector and text_to_speech).
set(HECQUIN_SOUND_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(hecquin_piper_speech STATIC ${HECQUIN_SOUND_SRC_ROOT}/tts/PiperSpeech.cpp)
target_link_libraries(hecquin_piper_speech PUBLIC hecquin_deps_sdl2 hecquin_deps_piper)
target_include_directories(hecquin_piper_speech PUBLIC ${HECQUIN_SOUND_SRC_ROOT})
