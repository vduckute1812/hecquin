# Text-to-Speech executable (Piper TTS)
add_executable(text_to_speech ${CMAKE_CURRENT_SOURCE_DIR}/text_to_speech.cpp)
target_link_libraries(text_to_speech PRIVATE hecquin_deps_sdl2 hecquin_deps_piper)

