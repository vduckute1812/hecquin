# Unit tests for the sound module.
#
# Each test is a tiny assertion-based main() — no framework dependency.
# Tests link directly against the narrow static libs declared in
# `cmake/sound_libs.cmake`, so we do not re-compile the same files per test.
#
# Layout: tests live in subfolders that mirror `sound/src/<module>/` so the
# physical tree carries the same grouping as the cmake sections below.
#   tests/ai/                       → hecquin_ai
#   tests/common/                   → header-only utilities
#   tests/config/                   → hecquin_config
#   tests/voice/                    → hecquin_voice_pipeline
#   tests/music/                    → hecquin_music
#   tests/tts/                      → hecquin_piper_speech
#   tests/learning/                 → hecquin_learning
#   tests/learning/ingest/          →   ingest pipeline subset
#   tests/learning/pronunciation/   → hecquin_pronunciation
#   tests/learning/pronunciation/drill/ → hecquin_drill subset
#   tests/learning/prosody/         → hecquin_prosody

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

# =============================================================================
# common/  — header-only helpers
# =============================================================================

# UTF-8 sanitizer (header-only) — regression for the 0xA0 CP-1252 crash.
hecquin_add_unit_test(hecquin_sound_test_utf8
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/common/test_utf8.cpp)

# POSIX shell single-quote escaper (header-only).
hecquin_add_unit_test(hecquin_sound_test_shell_escape
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/common/test_shell_escape.cpp)

# Subprocess RAII handle: spawn-read round-trip + kill-and-reap.
hecquin_add_unit_test(hecquin_sound_test_subprocess
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/common/test_subprocess.cpp)
target_link_libraries(hecquin_sound_test_subprocess PRIVATE hecquin_common)

# =============================================================================
# config/  — hecquin_config
# =============================================================================

# ConfigStore .env parsing.
hecquin_add_unit_test(hecquin_sound_test_config_store
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/config/test_config_store.cpp)
target_link_libraries(hecquin_sound_test_config_store PRIVATE hecquin_config)

# =============================================================================
# ai/  — hecquin_ai
# =============================================================================

# OpenAI chat content parser.
hecquin_add_unit_test(hecquin_sound_test_openai_chat
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/ai/test_openai_chat_content.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/ai/OpenAiChatContent.cpp)
target_link_libraries(hecquin_sound_test_openai_chat PRIVATE hecquin_deps_json)

# LocalIntentMatcher regex matrix.
hecquin_add_unit_test(hecquin_sound_test_local_intent
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/ai/test_local_intent_matcher.cpp)
target_link_libraries(hecquin_sound_test_local_intent PRIVATE hecquin_ai)

# RetryingHttpClient backoff + transient-classification behaviour.
# Compiled standalone (no hecquin_ai link) so the binary pulls in neither
# libcurl nor OpenSSL — that dodges macOS EDR agents which otherwise
# quarantine the signed artefact before it can even run.
hecquin_add_unit_test(hecquin_sound_test_net_retry
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/ai/test_retrying_http.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/ai/RetryingHttpClient.cpp)

# =============================================================================
# voice/  — hecquin_voice_pipeline (and pure components below it)
# =============================================================================

# VoiceListener secondary VAD gate — reject whispers / sparse background
# chatter before they reach Whisper.  Links the full pipeline library
# (SDL, whisper.cpp, Piper) but never calls into those dependencies, so the
# test stays deterministic and offline.
hecquin_add_unit_test(hecquin_sound_test_voice_listener_vad
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/voice/test_voice_listener_vad.cpp)
target_link_libraries(hecquin_sound_test_voice_listener_vad PRIVATE
    hecquin_voice_pipeline
    hecquin_piper_speech)

# Adaptive noise-floor tracker — pure component, no SDL or Whisper needed.
# Compiled standalone with just the tracker source so the test stays
# deterministic and links in seconds.
hecquin_add_unit_test(hecquin_sound_test_noise_floor_tracker
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/voice/test_noise_floor_tracker.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/voice/NoiseFloorTracker.cpp)
target_include_directories(hecquin_sound_test_noise_floor_tracker
    PRIVATE ${HECQUIN_SOUND_SRC_ROOT})

# MusicSideEffects dispatch contract — pure component (callbacks +
# nullable collector pointer), no SDL or Whisper actually exercised.
# Compiled standalone so it stays fast and dep-light.
hecquin_add_unit_test(hecquin_sound_test_music_side_effects
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/voice/test_music_side_effects.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/voice/MusicSideEffects.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/voice/AudioBargeInController.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/voice/NoiseFloorTracker.cpp)
target_include_directories(hecquin_sound_test_music_side_effects
    PRIVATE ${HECQUIN_SOUND_SRC_ROOT})

# WhisperPostFilter — pure transcript gates (annotation strip, min-alnum,
# no-speech probability cutoff).  Compiled standalone so the test does
# not need a GGML model or whisper.cpp at all.
hecquin_add_unit_test(hecquin_sound_test_whisper_post_filter
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/voice/test_whisper_post_filter.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/voice/WhisperPostFilter.cpp)
target_include_directories(hecquin_sound_test_whisper_post_filter
    PRIVATE ${HECQUIN_SOUND_SRC_ROOT})

# UtteranceRouter chain-of-responsibility ordering.
hecquin_add_unit_test(hecquin_sound_test_utterance_router
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/voice/test_utterance_router.cpp)
target_link_libraries(hecquin_sound_test_utterance_router PRIVATE
    hecquin_voice_pipeline
    hecquin_piper_speech)

# AudioBargeInController: pure coordinator (no SDL, no audio) — drives
# voice ON/OFF transitions + tick() and asserts the gain-setter /
# aborter sequencing for ducking / barge-in semantics.
hecquin_add_unit_test(hecquin_sound_test_audio_barge_in_controller
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/voice/test_audio_barge_in_controller.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/voice/AudioBargeInController.cpp)
target_include_directories(hecquin_sound_test_audio_barge_in_controller
    PRIVATE ${HECQUIN_SOUND_SRC_ROOT})

# =============================================================================
# tts/  — hecquin_piper_speech
# =============================================================================

# PiperFallbackBackend strategy chain — stubbed backends, no real Piper.
hecquin_add_unit_test(hecquin_sound_test_piper_backend_fallback
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/tts/test_piper_backend_fallback.cpp)
target_link_libraries(hecquin_sound_test_piper_backend_fallback PRIVATE
    hecquin_piper_speech)

# PcmRingQueue cv-backed drain signalling — no SDL needed, runs the
# queue in isolation with two producers + one drainer.
hecquin_add_unit_test(hecquin_sound_test_pcm_ring_queue
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/tts/test_pcm_ring_queue.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/tts/playback/PcmRingQueue.cpp)
target_include_directories(hecquin_sound_test_pcm_ring_queue
    PRIVATE ${HECQUIN_SOUND_SRC_ROOT})

# StreamingSdlPlayer gain-stage helper: drives the static apply_gain
# math directly without opening an SDL device.  Validates ramp slewing,
# saturation, and multi-buffer continuity used by the ducking path.
hecquin_add_unit_test(hecquin_sound_test_streaming_player_gain
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/tts/test_streaming_player_gain.cpp)
target_link_libraries(hecquin_sound_test_streaming_player_gain PRIVATE
    hecquin_piper_speech)

# =============================================================================
# music/  — hecquin_music
# =============================================================================

# MusicSession: provider contract (search → play → Action) via a fake.
# Does not link SDL-level bits — the FakeMusicProvider sidesteps
# StreamingSdlPlayer, and capture is passed as nullptr so AudioCapture
# is never constructed.
hecquin_add_unit_test(hecquin_sound_test_music_session
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/music/test_music_session.cpp)
target_link_libraries(hecquin_sound_test_music_session PRIVATE
    hecquin_music)

# yt-dlp shell-command builder: pure string assembly, no fork.
hecquin_add_unit_test(hecquin_sound_test_yt_dlp_commands
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/music/test_yt_dlp_commands.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/music/yt/YtDlpCommands.cpp)
target_include_directories(hecquin_sound_test_yt_dlp_commands
    PRIVATE ${HECQUIN_SOUND_SRC_ROOT})

# yt-dlp `--print` output parser: TAB / "\\t" fallback, preamble skipping.
hecquin_add_unit_test(hecquin_sound_test_yt_dlp_search_parser
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/music/test_yt_dlp_search_parser.cpp
    ${HECQUIN_SOUND_SRC_ROOT}/music/yt/YtDlpSearch.cpp)
target_include_directories(hecquin_sound_test_yt_dlp_search_parser
    PRIVATE ${HECQUIN_SOUND_SRC_ROOT})

# =============================================================================
# learning/  — hecquin_learning  (top level + per-subfolder sections)
# =============================================================================

# EmbeddingClient JSON round-trip through a fake HTTP client.
hecquin_add_unit_test(hecquin_sound_test_embedding_json
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/test_embedding_client_json.cpp)
target_link_libraries(hecquin_sound_test_embedding_json PRIVATE hecquin_learning)

# TextChunker boundary behaviour.
hecquin_add_unit_test(hecquin_sound_test_text_chunker
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/test_text_chunker.cpp)
target_link_libraries(hecquin_sound_test_text_chunker PRIVATE hecquin_learning)

# LearningStore SQLite round-trip (only if we have SQLite linked in).
if (HECQUIN_HAS_SQLITE)
    hecquin_add_unit_test(hecquin_sound_test_learning_store
        ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/test_learning_store.cpp)
    target_link_libraries(hecquin_sound_test_learning_store PRIVATE hecquin_learning)
endif ()

# ----- learning/ingest/ ------------------------------------------------------

# Ingestor chunking strategy dispatch (prose vs jsonl).
hecquin_add_unit_test(hecquin_sound_test_ingest_chunking_strategy
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/ingest/test_ingest_chunking_strategy.cpp)
target_link_libraries(hecquin_sound_test_ingest_chunking_strategy PRIVATE
    hecquin_learning)

# Ingest content fingerprint determinism.
hecquin_add_unit_test(hecquin_sound_test_content_fingerprint
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/ingest/test_content_fingerprint.cpp)
target_link_libraries(hecquin_sound_test_content_fingerprint PRIVATE
    hecquin_learning)

# ----- learning/pronunciation/ -----------------------------------------------

# PhonemeVocab greedy IPA tokenizer.
hecquin_add_unit_test(hecquin_sound_test_phoneme_vocab
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/pronunciation/test_phoneme_vocab.cpp)
target_link_libraries(hecquin_sound_test_phoneme_vocab PRIVATE hecquin_pronunciation)

# CTC forced aligner on a hand-crafted trellis.
hecquin_add_unit_test(hecquin_sound_test_ctc_aligner
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/pronunciation/test_ctc_aligner.cpp)
target_link_libraries(hecquin_sound_test_ctc_aligner PRIVATE hecquin_pronunciation)

# GOP logp → 0..100 scorer.
hecquin_add_unit_test(hecquin_sound_test_pronunciation_scorer
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/pronunciation/test_pronunciation_scorer.cpp)
target_link_libraries(hecquin_sound_test_pronunciation_scorer PRIVATE hecquin_pronunciation)

# ----- learning/prosody/ -----------------------------------------------------

# YIN pitch tracker on synthetic sine waves.
hecquin_add_unit_test(hecquin_sound_test_pitch_tracker
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/prosody/test_pitch_tracker.cpp)
target_link_libraries(hecquin_sound_test_pitch_tracker PRIVATE hecquin_prosody)

# IntonationScorer DTW + direction rule.
hecquin_add_unit_test(hecquin_sound_test_intonation_scorer
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/prosody/test_intonation_scorer.cpp)
target_link_libraries(hecquin_sound_test_intonation_scorer PRIVATE hecquin_prosody)

# Banded DTW numeric helper (identical / shifted / divergent inputs).
hecquin_add_unit_test(hecquin_sound_test_dtw_banded
    ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/prosody/test_dtw_banded.cpp)
target_link_libraries(hecquin_sound_test_dtw_banded PRIVATE hecquin_prosody)

# ----- learning/ (drill orchestrator + drill subfolder) ----------------------
# PronunciationDrillProcessor lives at learning/PronunciationDrillProcessor.cpp,
# while its helpers (sentence picker, reference-audio LRU) live under
# learning/pronunciation/drill/ — tests follow the same split.
if (HECQUIN_HAS_SQLITE)
    # PronunciationDrillProcessor end-to-end — fake emissions + injected
    # plan drive the feedback pipeline without espeak-ng, onnxruntime, or
    # Piper.
    hecquin_add_unit_test(hecquin_sound_test_pronunciation_drill
        ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/test_pronunciation_drill.cpp)
    target_link_libraries(hecquin_sound_test_pronunciation_drill PRIVATE hecquin_drill)

    # EnglishTutorProcessor RAG-context truncation + reply parsing.
    hecquin_add_unit_test(hecquin_sound_test_english_tutor_processor
        ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/test_english_tutor_processor.cpp)
    target_link_libraries(hecquin_sound_test_english_tutor_processor PRIVATE
        hecquin_learning)

    # Free-function tutor reply parser — pure, no DB / HTTP needed.
    hecquin_add_unit_test(hecquin_sound_test_tutor_reply_parser
        ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/tutor/test_tutor_reply_parser.cpp
        ${HECQUIN_SOUND_SRC_ROOT}/learning/tutor/TutorReplyParser.cpp)
    target_include_directories(hecquin_sound_test_tutor_reply_parser
        PRIVATE ${HECQUIN_SOUND_SRC_ROOT})

    # DrillSentencePicker spaced-repetition round-robin (no Piper, no DB).
    hecquin_add_unit_test(hecquin_sound_test_drill_sentence_picker
        ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/pronunciation/drill/test_drill_sentence_picker.cpp)
    target_link_libraries(hecquin_sound_test_drill_sentence_picker PRIVATE
        hecquin_drill)

    # DrillReferenceAudio LRU eviction + MRU bump.
    hecquin_add_unit_test(hecquin_sound_test_drill_reference_audio_lru
        ${HECQUIN_SOUND_TEST_SRC_ROOT}/learning/pronunciation/drill/test_drill_reference_audio_lru.cpp)
    target_link_libraries(hecquin_sound_test_drill_reference_audio_lru PRIVATE
        hecquin_drill)
endif ()
