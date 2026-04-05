# Voice detector executable (voice-to-text)
add_executable(voice_detector ${CMAKE_CURRENT_SOURCE_DIR}/voice_detector.cpp)
target_link_libraries(voice_detector PRIVATE hecquin_deps_whisper hecquin_deps_sdl2)

# Định nghĩa đường dẫn mặc định tới model
target_compile_definitions(voice_detector PRIVATE
    DEFAULT_MODEL_PATH="${MODELS_DIR}/ggml-base.bin"
)

