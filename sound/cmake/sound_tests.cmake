# Unit tests for the sound module.
#
# Each test is a tiny assertion-based main() — no framework dependency.
# Tests link directly against the narrow static libs declared in
# `cmake/sound_libs.cmake`, so we do not re-compile the same files per test.

if (NOT DEFINED HECQUIN_SOUND_SRC_ROOT)
    set(HECQUIN_SOUND_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif ()

option(HECQUIN_SOUND_BUILD_TESTS "Build hecquin_sound unit tests" ON)
if (NOT HECQUIN_SOUND_BUILD_TESTS)
    return()
endif ()

enable_testing()
set(HECQUIN_SOUND_TEST_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tests")

# Helper: add_executable + add_test + include dir + adhoc codesign in one call.
function(hecquin_add_unit_test name)
    set(sources ${ARGN})
    add_executable(${name} ${sources})
    target_include_directories(${name} PRIVATE ${HECQUIN_SOUND_SRC_ROOT})
    add_test(NAME ${name} COMMAND ${name})
    hecquin_adhoc_codesign(${name})
endfunction()

# OpenAI chat content parser — existing test.
hecquin_add_unit_test(hecquin_sound_test_openai_chat
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_openai_chat_content.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/ai/OpenAiChatContent.cpp)
target_link_libraries(hecquin_sound_test_openai_chat PRIVATE hecquin_deps_json)

# LocalIntentMatcher regex matrix.
hecquin_add_unit_test(hecquin_sound_test_local_intent
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_local_intent_matcher.cpp)
target_link_libraries(hecquin_sound_test_local_intent PRIVATE hecquin_ai)

# EmbeddingClient JSON round-trip through a fake HTTP client.
hecquin_add_unit_test(hecquin_sound_test_embedding_json
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_embedding_client_json.cpp)
target_link_libraries(hecquin_sound_test_embedding_json PRIVATE hecquin_learning)

# TextChunker boundary behaviour.
hecquin_add_unit_test(hecquin_sound_test_text_chunker
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_text_chunker.cpp)
target_link_libraries(hecquin_sound_test_text_chunker PRIVATE hecquin_learning)

# ConfigStore .env parsing.
hecquin_add_unit_test(hecquin_sound_test_config_store
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_config_store.cpp)
target_link_libraries(hecquin_sound_test_config_store PRIVATE hecquin_config)

# LearningStore SQLite round-trip (only if we have SQLite linked in).
if (HECQUIN_HAS_SQLITE)
    hecquin_add_unit_test(hecquin_sound_test_learning_store
        ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_learning_store.cpp)
    target_link_libraries(hecquin_sound_test_learning_store PRIVATE hecquin_learning)
endif ()
