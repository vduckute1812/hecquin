# SQLite3 + sqlite-vec (vendored amalgamation) integration.
#
# Strategy:
#   1. Find system SQLite3 (header + library).
#   2. Download the sqlite-vec amalgamation (single-file `sqlite-vec.c` + `sqlite-vec.h`)
#      into `third_party/sqlite-vec/` on first configure.
#   3. Compile it into a static library and expose it via the `hecquin_deps_sqlite_vec`
#      INTERFACE target.
#
# Opt out with -DHECQUIN_DISABLE_SQLITE_VEC=ON — then the learning module will be built
# without vector search (fallback to brute-force cosine in C++).

option(HECQUIN_DISABLE_SQLITE_VEC "Disable the sqlite-vec extension and use brute-force cosine" OFF)

# find_package(SQLite3) is available in CMake >= 3.14; fall back to manual search.
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.14")
    find_package(SQLite3 QUIET)
endif ()

if (NOT SQLite3_FOUND)
    find_path(SQLite3_INCLUDE_DIRS NAMES sqlite3.h)
    find_library(SQLite3_LIBRARIES NAMES sqlite3)
    if (SQLite3_INCLUDE_DIRS AND SQLite3_LIBRARIES)
        set(SQLite3_FOUND TRUE)
    endif ()
endif ()

add_library(hecquin_deps_sqlite_vec INTERFACE)

if (NOT SQLite3_FOUND)
    message(WARNING "SQLite3 not found — English-tutor / learning targets will be skipped. "
            "Install: macOS (Xcode CLT), Debian/Ubuntu: libsqlite3-dev")
    set(HECQUIN_HAS_SQLITE OFF CACHE INTERNAL "")
    return()
endif ()

set(HECQUIN_HAS_SQLITE ON CACHE INTERNAL "")
if (TARGET SQLite3::SQLite3)
    target_link_libraries(hecquin_deps_sqlite_vec INTERFACE SQLite3::SQLite3)
elseif (TARGET SQLite::SQLite3)
    target_link_libraries(hecquin_deps_sqlite_vec INTERFACE SQLite::SQLite3)
else ()
    target_include_directories(hecquin_deps_sqlite_vec INTERFACE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(hecquin_deps_sqlite_vec INTERFACE ${SQLite3_LIBRARIES})
endif ()
target_compile_definitions(hecquin_deps_sqlite_vec INTERFACE HECQUIN_WITH_SQLITE=1)

if (HECQUIN_DISABLE_SQLITE_VEC)
    message(STATUS "sqlite-vec disabled (HECQUIN_DISABLE_SQLITE_VEC=ON) — using brute-force cosine.")
    return()
endif ()

set(SQLITE_VEC_VERSION "v0.1.6")
set(SQLITE_VEC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite-vec")
set(SQLITE_VEC_C "${SQLITE_VEC_DIR}/sqlite-vec.c")
set(SQLITE_VEC_H "${SQLITE_VEC_DIR}/sqlite-vec.h")

if (NOT EXISTS "${SQLITE_VEC_C}" OR NOT EXISTS "${SQLITE_VEC_H}")
    file(MAKE_DIRECTORY "${SQLITE_VEC_DIR}")
    message(STATUS "Downloading sqlite-vec ${SQLITE_VEC_VERSION} amalgamation...")

    set(_base "https://github.com/asg017/sqlite-vec/releases/download/${SQLITE_VEC_VERSION}")
    set(_archive "${SQLITE_VEC_DIR}/sqlite-vec-${SQLITE_VEC_VERSION}-amalgamation.zip")

    file(DOWNLOAD
        "${_base}/sqlite-vec-0.1.6-amalgamation.zip"
        "${_archive}"
        STATUS _dl_status
        SHOW_PROGRESS
        TLS_VERIFY ON)

    list(GET _dl_status 0 _dl_code)
    if (NOT _dl_code EQUAL 0)
        file(REMOVE "${_archive}")
        message(WARNING "Failed to download sqlite-vec amalgamation (${_dl_status}). "
                "Learning targets will build without vector search. "
                "Manually drop sqlite-vec.c / sqlite-vec.h into ${SQLITE_VEC_DIR} and re-run cmake.")
        return()
    endif ()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E tar xzf "${_archive}"
        WORKING_DIRECTORY "${SQLITE_VEC_DIR}"
        RESULT_VARIABLE _unzip_rc)

    if (NOT _unzip_rc EQUAL 0 OR NOT EXISTS "${SQLITE_VEC_C}")
        message(WARNING "Could not extract sqlite-vec amalgamation (rc=${_unzip_rc}). "
                "Learning targets will build without vector search.")
        return()
    endif ()
    file(REMOVE "${_archive}")
endif ()

add_library(hecquin_sqlite_vec_obj STATIC "${SQLITE_VEC_C}")
set_target_properties(hecquin_sqlite_vec_obj PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON)
# SQLITE_CORE tells the amalgamation to skip the loadable-extension glue so we can
# call `sqlite3_vec_init(db, &err, NULL)` directly from our linked binary.
target_compile_definitions(hecquin_sqlite_vec_obj PRIVATE SQLITE_CORE=1)
target_include_directories(hecquin_sqlite_vec_obj PUBLIC "${SQLITE_VEC_DIR}")
if (TARGET SQLite3::SQLite3)
    target_link_libraries(hecquin_sqlite_vec_obj PUBLIC SQLite3::SQLite3)
elseif (TARGET SQLite::SQLite3)
    target_link_libraries(hecquin_sqlite_vec_obj PUBLIC SQLite::SQLite3)
else ()
    target_include_directories(hecquin_sqlite_vec_obj PUBLIC ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(hecquin_sqlite_vec_obj PUBLIC ${SQLite3_LIBRARIES})
endif ()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hecquin_sqlite_vec_obj PRIVATE -w)
endif ()

target_link_libraries(hecquin_deps_sqlite_vec INTERFACE hecquin_sqlite_vec_obj)
target_include_directories(hecquin_deps_sqlite_vec INTERFACE "${SQLITE_VEC_DIR}")
target_compile_definitions(hecquin_deps_sqlite_vec INTERFACE HECQUIN_WITH_SQLITE_VEC=1)

set(HECQUIN_HAS_SQLITE_VEC ON CACHE INTERNAL "")
message(STATUS "sqlite-vec enabled (${SQLITE_VEC_VERSION}) — vector search available.")
