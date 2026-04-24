# Build targets split by feature area.
# Order matters: pronunciation_drill.cmake publishes DEFAULT_PRONUNCIATION_*
# CACHE paths that english_tutor.cmake bakes into its binary, so drill must
# come first.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/voice_to_text.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/text_to_speech.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/pronunciation_drill.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/english_tutor.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sound_tests.cmake)

