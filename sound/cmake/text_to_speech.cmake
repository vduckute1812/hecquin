# Text-to-Speech executable (Piper TTS)
set(HECQUIN_SOUND_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(text_to_speech ${HECQUIN_SOUND_SRC_ROOT}/cli/TextToSpeech.cpp)
target_link_libraries(text_to_speech PRIVATE hecquin_piper_speech)

hecquin_adhoc_codesign(text_to_speech)
