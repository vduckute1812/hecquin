# nlohmann/json (single-header) integration.
#
# Downloads `json.hpp` once into `third_party/nlohmann/` and exposes it via the
# `hecquin_deps_json` INTERFACE target.  No submodule / FetchContent needed —
# the library is a single header so a bare `file(DOWNLOAD …)` is enough.

if (TARGET hecquin_deps_json)
    return()
endif ()

set(NLOHMANN_JSON_VERSION "v3.11.3")
set(NLOHMANN_JSON_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann")
set(NLOHMANN_JSON_HEADER  "${NLOHMANN_JSON_DIR}/nlohmann/json.hpp")

if (NOT EXISTS "${NLOHMANN_JSON_HEADER}")
    file(MAKE_DIRECTORY "${NLOHMANN_JSON_DIR}/nlohmann")
    message(STATUS "Downloading nlohmann/json ${NLOHMANN_JSON_VERSION} single-header...")

    file(DOWNLOAD
        "https://github.com/nlohmann/json/releases/download/${NLOHMANN_JSON_VERSION}/json.hpp"
        "${NLOHMANN_JSON_HEADER}"
        STATUS _json_dl_status
        SHOW_PROGRESS
        TLS_VERIFY ON)

    list(GET _json_dl_status 0 _json_dl_code)
    if (NOT _json_dl_code EQUAL 0)
        file(REMOVE "${NLOHMANN_JSON_HEADER}")
        message(FATAL_ERROR
            "Failed to download nlohmann/json (${_json_dl_status}). "
            "Drop json.hpp into ${NLOHMANN_JSON_DIR}/nlohmann/ manually and re-run cmake.")
    endif ()
endif ()

add_library(hecquin_deps_json INTERFACE)
target_include_directories(hecquin_deps_json INTERFACE "${NLOHMANN_JSON_DIR}")
# Smaller binary + faster compile — we only need runtime JSON, not reflection.
target_compile_definitions(hecquin_deps_json INTERFACE
    JSON_USE_IMPLICIT_CONVERSIONS=0)
