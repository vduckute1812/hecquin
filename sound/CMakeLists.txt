cmake_minimum_required(VERSION 3.10)
project(hecquin_sound LANGUAGES C CXX)

# Split CMake configuration into small modules
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/project_options.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps_whisper.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps_piper.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps_sdl2.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependency_libraries.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/targets.cmake)
