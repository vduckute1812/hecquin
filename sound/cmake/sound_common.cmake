# Tiny dependency-free utility lib: shell quoting, subprocess RAII handle.
# Used by `hecquin_music` (yt-dlp / ffmpeg pipelines), the shell-backed
# Piper backend in `hecquin_piper_speech`, and any future subsystem that
# spawns a child process or builds a `/bin/sh -c` command.
#
# Pulled into its own .cmake so `piper_speech.cmake` (loaded earlier
# from the top-level `CMakeLists.txt`) can declare
# `hecquin_piper_speech` PUBLIC-link against it.  The header-only helpers
# (StringUtils, EnvParse, Utf8) live next to the .cpp under
# `src/common/` and are picked up through the lib's public include
# directory.

if (NOT DEFINED HECQUIN_SOUND_SRC_ROOT)
    set(HECQUIN_SOUND_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif ()

if (NOT TARGET hecquin_common)
    add_library(hecquin_common STATIC
        ${HECQUIN_SOUND_SRC_ROOT}/common/Subprocess.cpp
    )
    target_include_directories(hecquin_common PUBLIC ${HECQUIN_SOUND_SRC_ROOT})
    set_target_properties(hecquin_common PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()
