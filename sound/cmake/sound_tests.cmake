# Optional unit tests for the sound module (no third-party deps beyond the STL).
if (NOT DEFINED HECQUIN_SOUND_SRC_ROOT)
    set(HECQUIN_SOUND_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif ()

option(HECQUIN_SOUND_BUILD_TESTS "Build hecquin_sound unit tests" ON)

if (HECQUIN_SOUND_BUILD_TESTS)
    enable_testing()

    set(HECQUIN_SOUND_TEST_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tests")

    add_executable(hecquin_sound_test_openai_chat
        ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_openai_chat_content.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/ai/OpenAiChatContent.cpp
    )
    target_include_directories(hecquin_sound_test_openai_chat PRIVATE ${HECQUIN_SOUND_SRC_ROOT})

    add_test(NAME hecquin_sound_openai_chat_content COMMAND hecquin_sound_test_openai_chat)

    hecquin_adhoc_codesign(hecquin_sound_test_openai_chat)
endif ()
