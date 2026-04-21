# ONNX Runtime integration for the pronunciation / phoneme-scoring module.
#
# Strategy:
#   1. Honour an explicit ONNXRUNTIME_ROOT (from env or -D) first.
#   2. Otherwise probe the standard Homebrew / apt install locations and the
#      project-local fallback `.env/<platform>/onnxruntime/`.
#   3. If nothing is found, define the INTERFACE target without HECQUIN_WITH_ONNX
#      so translation units can `#ifdef` the scoring code out cleanly — the rest
#      of the build still works, the drill binary just reports "scoring
#      unavailable" at runtime, mirroring how AiClientConfig treats libcurl.
#
# Opt out entirely with -DHECQUIN_DISABLE_ONNX=ON.

option(HECQUIN_DISABLE_ONNX "Disable onnxruntime-backed pronunciation scoring" OFF)

if (TARGET hecquin_deps_onnxruntime)
    return()
endif ()

add_library(hecquin_deps_onnxruntime INTERFACE)

if (HECQUIN_DISABLE_ONNX)
    message(STATUS "onnxruntime disabled (HECQUIN_DISABLE_ONNX=ON) — pronunciation scoring will be stubbed.")
    return()
endif ()

# --- Locate headers + library ------------------------------------------------

set(_ort_root_hints
    "$ENV{ONNXRUNTIME_ROOT}"
    "${ONNXRUNTIME_ROOT}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.env/mac/onnxruntime"
    "${CMAKE_CURRENT_SOURCE_DIR}/.env/rpi/onnxruntime"
    "${CMAKE_CURRENT_SOURCE_DIR}/.env/linux/onnxruntime"
    "${CMAKE_CURRENT_SOURCE_DIR}/.env/shared/onnxruntime"
    "/opt/homebrew/opt/onnxruntime"
    "/opt/homebrew"
    "/usr/local"
    "/usr"
)

find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS ${_ort_root_hints}
    PATH_SUFFIXES include include/onnxruntime include/onnxruntime/core/session)

find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    HINTS ${_ort_root_hints}
    PATH_SUFFIXES lib lib64)

if (ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    message(STATUS "Found onnxruntime: ${ONNXRUNTIME_LIBRARY}")
    message(STATUS "  include dir: ${ONNXRUNTIME_INCLUDE_DIR}")
    target_include_directories(hecquin_deps_onnxruntime INTERFACE "${ONNXRUNTIME_INCLUDE_DIR}")
    target_link_libraries(hecquin_deps_onnxruntime INTERFACE "${ONNXRUNTIME_LIBRARY}")
    target_compile_definitions(hecquin_deps_onnxruntime INTERFACE HECQUIN_WITH_ONNX=1)
    set(HECQUIN_HAS_ONNX ON CACHE INTERNAL "")
else ()
    message(STATUS "onnxruntime not found — pronunciation scoring will be stubbed. "
            "Install via: brew install onnxruntime (macOS) / apt install libonnxruntime-dev (Debian) "
            "or drop a prebuilt tree into .env/<platform>/onnxruntime/.")
    set(HECQUIN_HAS_ONNX OFF CACHE INTERNAL "")
endif ()
