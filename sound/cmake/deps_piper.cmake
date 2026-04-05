# Shared model paths.
set(MODELS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.env/shared/models)

# Piper defaults and executable discovery.
set(PIPER_MODEL_DIR ${MODELS_DIR}/piper)
if(DEFINED DEFAULT_PIPER_MODEL_PATH)
    set(PIPER_DEFAULT_MODEL "${DEFAULT_PIPER_MODEL_PATH}")
else()
    set(PIPER_DEFAULT_MODEL "${PIPER_MODEL_DIR}/en_US-lessac-medium.onnx")
endif()

find_program(PIPER_EXECUTABLE piper
    HINTS
        ${CMAKE_CURRENT_SOURCE_DIR}/.env/mac/piper
        ${CMAKE_CURRENT_SOURCE_DIR}/.env/rpi/piper
        ${CMAKE_CURRENT_SOURCE_DIR}/.env/piper
        $ENV{HOME}/.local/bin
        /usr/local/bin
)

if(PIPER_EXECUTABLE)
    message(STATUS "Found Piper TTS: ${PIPER_EXECUTABLE}")
else()
    message(STATUS "Piper TTS not found. Run: ./dev.sh piper:install")
    set(PIPER_EXECUTABLE "piper")
endif()

