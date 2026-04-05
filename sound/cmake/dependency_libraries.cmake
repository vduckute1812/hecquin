# Centralize third-party bindings as reusable interface libraries.
add_library(hecquin_deps_sdl2 INTERFACE)
target_link_libraries(hecquin_deps_sdl2 INTERFACE SDL2::SDL2)

add_library(hecquin_deps_whisper INTERFACE)
if(whisper_FOUND)
    target_link_libraries(hecquin_deps_whisper INTERFACE whisper)
elseif(WHISPER_LIBRARY)
    target_include_directories(hecquin_deps_whisper INTERFACE ${WHISPER_INCLUDE_DIR})
    target_link_libraries(hecquin_deps_whisper INTERFACE ${WHISPER_LIBRARY})
else()
    message(FATAL_ERROR "Whisper dependency not found. Run: ./dev.sh whisper:clone && ./dev.sh whisper:build")
endif()

add_library(hecquin_deps_piper INTERFACE)
target_compile_definitions(hecquin_deps_piper INTERFACE
    DEFAULT_PIPER_MODEL_PATH="${PIPER_DEFAULT_MODEL}"
    PIPER_EXECUTABLE="${PIPER_EXECUTABLE}"
)

