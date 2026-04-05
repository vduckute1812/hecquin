# Whisper dependency discovery.
if(NOT DEFINED WHISPER_INSTALL_DIR)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(_HECQUIN_WHISPER_PLATFORM "mac")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|armv7l)$")
        set(_HECQUIN_WHISPER_PLATFORM "rpi")
    else()
        set(_HECQUIN_WHISPER_PLATFORM "linux")
    endif()

    set(_HECQUIN_PLATFORM_WHISPER_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.env/${_HECQUIN_WHISPER_PLATFORM}/whisper-install")
    set(_HECQUIN_LEGACY_WHISPER_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.env/whisper-install")

    if(EXISTS "${_HECQUIN_PLATFORM_WHISPER_INSTALL_DIR}")
        set(WHISPER_INSTALL_DIR "${_HECQUIN_PLATFORM_WHISPER_INSTALL_DIR}" CACHE PATH "Path to whisper install directory")
    else()
        set(WHISPER_INSTALL_DIR "${_HECQUIN_LEGACY_WHISPER_INSTALL_DIR}" CACHE PATH "Path to whisper install directory")
    endif()
endif()

list(APPEND CMAKE_PREFIX_PATH ${WHISPER_INSTALL_DIR})

find_package(whisper CONFIG QUIET)

if(whisper_FOUND)
    message(STATUS "Found whisper: ${WHISPER_INSTALL_DIR}")
else()
    message(STATUS "whisper package not found, using manual paths")

    set(WHISPER_INCLUDE_DIR ${WHISPER_INSTALL_DIR}/include)
    set(WHISPER_LIB_DIR ${WHISPER_INSTALL_DIR}/lib)

    unset(WHISPER_LIBRARY CACHE)

    find_library(WHISPER_LIBRARY
        NAMES whisper
        PATHS ${WHISPER_LIB_DIR}
        NO_DEFAULT_PATH
    )

    if(NOT WHISPER_LIBRARY)
        message(WARNING "whisper library not found. Run: ./dev.sh whisper:clone && ./dev.sh whisper:build")
    endif()
endif()

