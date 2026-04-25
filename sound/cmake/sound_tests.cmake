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

# UTF-8 sanitizer (header-only) — regression for the 0xA0 CP-1252 crash.
hecquin_add_unit_test(hecquin_sound_test_utf8
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_utf8.cpp)

# LocalIntentMatcher regex matrix.
hecquin_add_unit_test(hecquin_sound_test_local_intent
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_local_intent_matcher.cpp)
target_link_libraries(hecquin_sound_test_local_intent PRIVATE hecquin_ai)

# RetryingHttpClient backoff + transient-classification behaviour.
# Compiled standalone (no hecquin_ai link) so the binary pulls in neither
# libcurl nor OpenSSL — that dodges macOS EDR agents which otherwise
# quarantine the signed artefact before it can even run.
hecquin_add_unit_test(hecquin_sound_test_net_retry
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_retrying_http.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/ai/RetryingHttpClient.cpp)

# VoiceListener secondary VAD gate — reject whispers / sparse background
# chatter before they reach Whisper.  Links the full pipeline library
# (SDL, whisper.cpp, Piper) but never calls into those dependencies, so the
# test stays deterministic and offline.
hecquin_add_unit_test(hecquin_sound_test_voice_listener_vad
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_voice_listener_vad.cpp)
target_link_libraries(hecquin_sound_test_voice_listener_vad PRIVATE
    hecquin_voice_pipeline
    hecquin_piper_speech)

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

# Pronunciation & prosody — PhonemeVocab greedy IPA tokenizer.
hecquin_add_unit_test(hecquin_sound_test_phoneme_vocab
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_phoneme_vocab.cpp)
target_link_libraries(hecquin_sound_test_phoneme_vocab PRIVATE hecquin_pronunciation)

# Pronunciation & prosody — CTC forced aligner on a hand-crafted trellis.
hecquin_add_unit_test(hecquin_sound_test_ctc_aligner
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_ctc_aligner.cpp)
target_link_libraries(hecquin_sound_test_ctc_aligner PRIVATE hecquin_pronunciation)

# Pronunciation & prosody — GOP logp → 0..100 scorer.
hecquin_add_unit_test(hecquin_sound_test_pronunciation_scorer
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_pronunciation_scorer.cpp)
target_link_libraries(hecquin_sound_test_pronunciation_scorer PRIVATE hecquin_pronunciation)

# Pronunciation & prosody — YIN pitch tracker on synthetic sine waves.
hecquin_add_unit_test(hecquin_sound_test_pitch_tracker
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_pitch_tracker.cpp)
target_link_libraries(hecquin_sound_test_pitch_tracker PRIVATE hecquin_prosody)

# Pronunciation & prosody — IntonationScorer DTW + direction rule.
hecquin_add_unit_test(hecquin_sound_test_intonation_scorer
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_intonation_scorer.cpp)
target_link_libraries(hecquin_sound_test_intonation_scorer PRIVATE hecquin_prosody)

# PronunciationDrillProcessor end-to-end — fake emissions + injected plan
# drive the feedback pipeline without espeak-ng, onnxruntime, or Piper.
if (HECQUIN_HAS_SQLITE)
    hecquin_add_unit_test(hecquin_sound_test_pronunciation_drill
        ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_pronunciation_drill.cpp)
    target_link_libraries(hecquin_sound_test_pronunciation_drill PRIVATE hecquin_drill)

    # EnglishTutorProcessor RAG-context truncation + reply parsing.
    hecquin_add_unit_test(hecquin_sound_test_english_tutor_processor
        ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_english_tutor_processor.cpp)
    target_link_libraries(hecquin_sound_test_english_tutor_processor PRIVATE
        hecquin_learning)

    # DrillSentencePicker spaced-repetition round-robin (no Piper, no DB).
    hecquin_add_unit_test(hecquin_sound_test_drill_sentence_picker
        ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_drill_sentence_picker.cpp)
    target_link_libraries(hecquin_sound_test_drill_sentence_picker PRIVATE
        hecquin_drill)

    # DrillReferenceAudio LRU eviction + MRU bump.
    hecquin_add_unit_test(hecquin_sound_test_drill_reference_audio_lru
        ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_drill_reference_audio_lru.cpp)
    target_link_libraries(hecquin_sound_test_drill_reference_audio_lru PRIVATE
        hecquin_drill)
endif ()

# PiperFallbackBackend strategy chain — stubbed backends, no real Piper.
hecquin_add_unit_test(hecquin_sound_test_piper_backend_fallback
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_piper_backend_fallback.cpp)
target_link_libraries(hecquin_sound_test_piper_backend_fallback PRIVATE
    hecquin_piper_speech)

# Ingestor chunking strategy dispatch (prose vs jsonl).
hecquin_add_unit_test(hecquin_sound_test_ingest_chunking_strategy
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_ingest_chunking_strategy.cpp)
target_link_libraries(hecquin_sound_test_ingest_chunking_strategy PRIVATE
    hecquin_learning)

# Ingest content fingerprint determinism.
hecquin_add_unit_test(hecquin_sound_test_content_fingerprint
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_content_fingerprint.cpp)
target_link_libraries(hecquin_sound_test_content_fingerprint PRIVATE
    hecquin_learning)

# UtteranceRouter chain-of-responsibility ordering.
hecquin_add_unit_test(hecquin_sound_test_utterance_router
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_utterance_router.cpp)
target_link_libraries(hecquin_sound_test_utterance_router PRIVATE
    hecquin_voice_pipeline
    hecquin_piper_speech)

# MusicSession: provider contract (search → play → Action) via a fake.
# Does not link SDL-level bits — the FakeMusicProvider sidesteps
# StreamingSdlPlayer, and capture is passed as nullptr so AudioCapture
# is never constructed.
hecquin_add_unit_test(hecquin_sound_test_music_session
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/test_music_session.cpp)
target_link_libraries(hecquin_sound_test_music_session PRIVATE
    hecquin_music)
