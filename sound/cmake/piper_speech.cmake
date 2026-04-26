# Shared Piper + SDL playback (used by voice_detector and text_to_speech).
#
# Internally the library is split into runtime / backend / playback / wav
# sub-trees under src/tts/ — see ARCHITECTURE.md for the Strategy +
# Template-Method layout.  Callers continue to depend on the flat public
# API in tts/PiperSpeech.hpp.
set(HECQUIN_SOUND_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(hecquin_piper_speech STATIC
    ${HECQUIN_SOUND_SRC_ROOT}/tts/PiperSpeech.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/tts/PlayPipeline.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/tts/runtime/PiperRuntime.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/tts/wav/WavReader.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/tts/backend/PiperSpawn.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/tts/backend/PiperPipeBackend.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/tts/backend/PiperShellBackend.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/tts/backend/PiperFallbackBackend.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/tts/playback/SdlAudioDevice.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/tts/playback/SdlMonoDevice.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/tts/playback/PcmRingQueue.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/tts/playback/BufferedSdlPlayer.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/tts/playback/StreamingSdlPlayer.cpp
)
target_link_libraries(hecquin_piper_speech PUBLIC
    hecquin_common
    hecquin_deps_sdl2
    hecquin_deps_piper)
target_include_directories(hecquin_piper_speech PUBLIC ${HECQUIN_SOUND_SRC_ROOT})
