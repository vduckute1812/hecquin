# Voice detector executable (voice-to-text + command / AI routing)
if (NOT DEFINED HECQUIN_SOUND_SRC_ROOT)
    set(HECQUIN_SOUND_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif ()

add_executable(voice_detector
    ${HECQUIN_SOUND_SRC_ROOT}/voice/VoiceDetector.cpp
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
target_link_libraries(voice_detector PRIVATE hecquin_deps_whisper hecquin_deps_sdl2 hecquin_deps_curl
    hecquin_piper_speech)

# Định nghĩa đường dẫn mặc định tới model
target_compile_definitions(voice_detector PRIVATE
    DEFAULT_MODEL_PATH="${MODELS_DIR}/ggml-base.bin"
    DEFAULT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/.env/config.env"
    DEFAULT_PROMPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.env/prompts"
)
