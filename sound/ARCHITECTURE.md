# Hecquin Sound Module — Architecture & Logic

> This document is the **developer / maintainer guide** — source layout, component internals,
> data-flow diagrams, SQLite schema, threading model, and CMake internals.
> For **setup, CLI usage, configuration keys, and troubleshooting**, see
> [`README.md`](./README.md).

## Overview

The Hecquin Sound Module is a cross-platform audio processing subsystem for the Robot Tutor project. It provides four core capabilities:

- **Voice-to-Text**: Speech recognition via whisper.cpp
- **Text-to-Speech**: Speech synthesis via Piper TTS
- **English Tutor (Lesson mode)**: Grammar correction via RAG + chat completions
- **Pronunciation & Intonation Drill (Drill mode)**: Per-phoneme and prosody scoring against a known reference, fully local (wav2vec2 + YIN)

The system captures microphone audio, detects speech with VAD (Voice Activity Detection), transcribes it, routes the transcript (and, for Drill mode, the raw PCM) to a local command matcher, the tutor processor, or the drill processor, then speaks the reply back through the speaker.

---

## Pipeline

```
Microphone
    │
    ▼
AudioCapture (SDL2, 16 kHz mono float32)
    │
    ▼
VoiceListener — polls every 50 ms
    │   VAD: RMS > 0.02 for ≥ 500 ms → speech start
    │         800 ms of silence  → speech end
    │   Secondary gate (pure `evaluate_secondary_gate`) rejects with an
    │   explicit `too_quiet` / `too_sparse` reason before Whisper runs.
    ▼
WhisperEngine — one-shot decode (language + n_threads + beam_size from `WhisperConfig`)
    │
    ▼
CommandProcessor
    ├── Local regex matching (no network, instant; patterns from `AppConfig`)
    │       "turn on/off" + device        → DeviceAction
    │       "tell me a story"             → TopicSearchAction
    │       "open music"                  → MusicAction::prompt  (enters Music mode)
    │       "stop/cancel/exit/close/end music" → MusicAction::cancel  (aborts playback, leaves Music mode)
    │       "pause music"                 → MusicAction::pause   (best-effort suspend)
    │       "continue/resume music"       → MusicAction::resume  (counterpart of pause)
    │       "start english lesson" / "exit lesson" → LessonModeToggle
    │       "start pronunciation drill" / "exit drill" → DrillModeToggle
    │
    └── External API fallback (synchronous; wrapped in RetryingHttpClient → LoggingHttpClient → CurlHttpClient)
            POST /v1/chat/completions → ExternalApiAction
    │
    ▼
PiperSpeech — persistent stdin subprocess (or in-process `piper_synthesize_to_buffer`)
            → streaming SDL2 playback (producer/consumer ring)
    │
    ▼
Speaker
```

---

## Source Layout

```
src/
├── voice/
│   ├── AudioCapture.hpp/cpp      — SDL2 microphone capture (+ `MuteGuard` RAII, `snapshotRecent` tail copy)
│   ├── AudioCaptureConfig.hpp    — POD config (no SDL include)
│   ├── WhisperEngine.hpp/cpp     — whisper.cpp wrapper + env-driven `WhisperConfig` (language / threads / beam); `transcribe()` delegates to `build_wparams` + `collect_segments` helpers
│   ├── VoiceListenerConfig.hpp/cpp — `VoiceListenerConfig` POD + `apply_env_overrides()` (extracted from `VoiceListener.hpp`)
│   ├── PipelineEvent.hpp         — `PipelineEvent` + `PipelineEventSink` typedefs (extracted from `VoiceListener.hpp`)
│   ├── WhisperPostFilter.hpp/cpp — pure transcript gates (annotation strip, min-alnum, no-speech) — extracted from `WhisperEngine`
│   ├── ListenerMode.hpp          — `ListenerMode { Assistant, Lesson, Drill, Music }` enum (own header to break cycles)
│   ├── VoiceListener.hpp/cpp     — thin coordinator: poll loop + mode state machine
│   ├── ActionSideEffectRegistry.hpp/cpp — data-driven `ActionKind → {ModeChange, music_hook}` table; replaces the old switch in `VoiceListener::apply_local_intent_side_effects_`
│   ├── MusicSideEffects.hpp/cpp  — listener-side music-mode toggles + speaker-bleed gate
│   ├── MusicWiring.hpp/cpp       — `install_music_wiring(listener, MusicConfig)` builds `MusicProvider` + `MusicSession` + 4 callbacks; replaces the copy that used to live in every voice main
│   ├── UtteranceCollector.hpp/cpp— primary VAD, collection timers, full-buffer snapshot on close
│   ├── SecondaryVadGate.hpp/cpp  — pure `evaluate_secondary_gate(...)` (mean-RMS + voiced-ratio)
│   ├── TtsResponsePlayer.hpp/cpp — TTS sanitise + `MuteGuard` wrap + Piper playback
│   ├── UtteranceRouter.hpp/cpp   — Chain of Responsibility: local-intent → drill cb → tutor cb → chat
│   ├── VoiceApp.hpp/cpp          — shared bootstrap for voice executables
│   └── VoiceDetector.cpp         — voice_detector executable entry point
├── ai/
│   ├── IHttpClient.hpp           — abstract HTTP transport (testable)
│   ├── HttpClient.hpp/cpp        — libcurl `http_post_json` + `CurlHttpClient`
│   ├── LoggingHttpClient.hpp/cpp — decorator: IHttpClient → ApiCallSink telemetry
│   ├── RetryingHttpClient.hpp/cpp— decorator: exponential backoff on 5xx / 429 / transport errors
│   ├── LocalIntentMatcher.hpp/cpp— regex-based local intents, patterns injected from `AppConfig`
│   ├── ChatClient.hpp/cpp        — remote LLM client (IHttpClient injected)
│   ├── CommandProcessor.hpp/cpp  — façade: matcher + chat
│   ├── HttpReplyBuckets.hpp/cpp  — shared `short_reply_for_status(...)` used by chat + tutor error paths
│   └── OpenAiChatContent.hpp/cpp — nlohmann/json response extractor
├── observability/
│   └── Log.hpp                   — tiny structured logger (pretty / JSON via `HECQUIN_LOG_FORMAT`, level via `HECQUIN_LOG_LEVEL`)
├── common/                      — cross-cutting C++ utilities used by ai / music / tts / voice
│   ├── StringUtils.hpp          — header-only trim / lower / starts_with
│   ├── EnvParse.hpp             — header-only `read_string / parse_int / parse_float / parse_size` for `HECQUIN_*` env vars
│   ├── ShellEscape.hpp          — header-only `posix_sh_single_quote(...)` (single source of truth for sh escaping)
│   ├── Subprocess.hpp/cpp       — RAII wrapper around `fork`/`exec` + stdout pipe (`spawn_read`, `kill_and_reap`)
│   └── Utf8.hpp                  — `sanitize_utf8` + `utf8::{codepoint_length, is_continuation, align_to_codepoint}` (header-only; shared by chunker + IPA tokenizer)
├── cli/                          — bits shared across executable entry points
│   └── DefaultPaths.hpp          — single source of truth for `DEFAULT_MODEL_PATH` / `DEFAULT_PIPER_MODEL_PATH` / `DEFAULT_CONFIG_PATH` / `DEFAULT_PROMPTS_DIR` / `DEFAULT_PRONUNCIATION_*` (CMake fallbacks for compiles outside the project build)
├── music/                        — pluggable music subsystem (yt-dlp + ffmpeg today)
│   ├── MusicProvider.hpp         — `MusicProvider` abstract interface (search / play / stop / pause / resume)
│   ├── MusicFactory.hpp/cpp      — `make_provider_from_config(MusicConfig)` selects the back-end
│   ├── MusicSession.hpp/cpp      — orchestrates one search → play → cancel session on a worker thread
│   ├── YouTubeMusicProvider.hpp/cpp — thin orchestrator: wires `yt/*` through `common::Subprocess`
│   └── yt/                       — split out of `YouTubeMusicProvider`
│       ├── YtDlpCommands.hpp/cpp     — pure `build_search_command` / `build_playback_command` (no I/O)
│       ├── YtDlpSearch.hpp/cpp       — pure `parse_search_output(...)` (TAB / `\\t` fallback, blank-line skip)
│       └── YtPlaybackPipeline.hpp/cpp— owns the read loop + `StreamingSdlPlayer` lifecycle for one playback
├── learning/
│   ├── store/                             — SQLite-backed persistence (façade + `detail::` free functions)
│   │   ├── LearningStore.hpp              — Public façade (single connection, thin forwarders)
│   │   ├── LearningStore.cpp              — lifecycle + metadata (open/close, kv)
│   │   ├── LearningStoreMigrations.cpp    — all DDL (schema v2)
│   │   ├── LearningStoreDocuments.cpp     — forwards to `detail::DocumentsOps`
│   │   ├── LearningStoreVectorSearch.cpp  — forwards to `detail::VectorSearchOps`
│   │   ├── LearningStoreSessions.cpp      — forwards to `detail::SessionsOps`
│   │   ├── LearningStorePronunciation.cpp — forwards to `detail::PronunciationOps`
│   │   ├── LearningStoreApiCalls.cpp      — forwards to `detail::ApiCallsOps`
│   │   ├── detail/*.hpp                   — per-aggregate free-function headers (take `sqlite3*` + StatementCache&)
│   │   └── internal/SqliteHelpers.hpp     — private RAII glue (StmtGuard, Transaction, prepare_or_log, …)
│   ├── EmbeddingClient.hpp/cpp   — Gemini embeddings (OpenAI-compat, batched)
│   ├── Ingestor.hpp/cpp          — thin coordinator over the `ingest/` pipeline below
│   ├── ingest/                   — curriculum ingestion split by responsibility
│   │   ├── FileDiscovery.hpp/cpp     — walk + extension / kind filter
│   │   ├── ContentFingerprint.hpp/cpp— FNV-1a over UTF-8-sanitised content
│   │   ├── ChunkingStrategy.hpp/cpp  — `IChunker` Strategy + factory
│   │   ├── ProseChunker.hpp/cpp      — `chunk_text`-based chunker
│   │   ├── JsonlChunker.hpp/cpp      — line-boundary chunker for jsonl / json
│   │   ├── EmbeddingBatcher.hpp/cpp  — batched embed + per-chunk fallback
│   │   ├── DocumentPersister.hpp/cpp — build `DocumentRecord` + upsert
│   │   └── ProgressReporter.hpp/cpp  — CLI progress + ETA lines
│   ├── Vocabulary.hpp/cpp        — shared `normalise(word)` (lowercase alpha + apostrophe)
│   ├── TextChunker.hpp/cpp       — standalone chunking utility (prose + lines)
│   ├── RetrievalService.hpp/cpp  — vector search helpers
│   ├── ProgressTracker.hpp/cpp   — per-user learning log (grammar + pronunciation)
│   ├── EnglishTutorProcessor.*   — thin coordinator: RAG → chat → reply parse → progress log (delegates to `tutor/`)
│   ├── tutor/                    — single-responsibility helpers behind `EnglishTutorProcessor`
│   │   ├── TutorContextBuilder.hpp/cpp — wraps `RetrievalService::build_context` with the tutor's RAG knobs
│   │   ├── TutorChatRequest.hpp/cpp    — pure `build_chat_body(ai, user, ctx)` (system + user message, UTF-8-safe dump)
│   │   └── TutorReplyParser.hpp/cpp    — pure `parse_tutor_reply(raw, fallback)` → `GrammarCorrectionAction`
│   ├── PronunciationDrillProcessor.hpp/cpp — thin coordinator for the drill pipeline
│   ├── pronunciation/            — wav2vec2 + CTC forced alignment + GOP
│   │   ├── PhonemeTypes.hpp      — shared data structs (Emissions, AlignSegment, …)
│   │   ├── PhonemeVocab.hpp/cpp  — IPA ↔ id tokenizer (greedy longest-match)
│   │   ├── PhonemeModel.hpp/cpp  — ONNX Runtime wrapper (optional: HECQUIN_WITH_ONNX)
│   │   ├── G2P.hpp/cpp           — espeak-ng --ipa=3 → target phoneme ids
│   │   ├── CtcAligner.hpp/cpp    — Viterbi forced alignment
│   │   ├── PronunciationScorer.hpp/cpp — logp → 0..100 per phoneme / word / overall
│   │   └── drill/                — drill collaborators (each with a single responsibility)
│   │       ├── DrillSentencePicker.hpp/cpp    — pool + phoneme index + spaced-repetition bias
│   │       ├── DrillReferenceAudio.hpp/cpp    — Piper synth + LRU<PCM,contour> + SDL replay
│   │       ├── DrillScoringPipeline.hpp/cpp   — Template Method: plan → align → score → intonation
│   │       └── DrillProgressLogger.hpp/cpp    — per-phoneme JSON + ProgressTracker bridge
│   ├── prosody/                  — local intonation pipeline
│   │   ├── PitchTracker.hpp/cpp  — YIN F0 contour + per-frame RMS
│   │   ├── Dtw.hpp/cpp            — `dtw_mean_abs_banded` (banded DTW, rolling rows) — extracted from `IntonationScorer.cpp`
│   │   └── IntonationScorer.hpp/cpp — semitone DTW + final-direction rule
│   └── cli/                      — `LearningApp` shared bootstrap + `english_ingest`, `english_tutor`, `pronunciation_drill` entries
│       └── StoreApiSink.hpp/cpp  — `make_store_api_call_sink(LearningStore&)` — shared by `english_ingest` + `english_tutor` mains
├── actions/
│   ├── ActionKind.hpp            — enum: None / LocalDevice / TopicSearch / MusicSearchPrompt / MusicPlayback / MusicNotFound / MusicCancel / MusicPause / MusicResume / ExternalApi / EnglishLesson / GrammarCorrection / LessonModeToggle / PronunciationFeedback / DrillModeToggle
│   ├── Action.hpp                — {kind, reply, transcript}
│   ├── DeviceAction.hpp          — power control confirmation text
│   ├── ExternalApiAction.hpp     — wraps API response
│   ├── TopicSearchAction.hpp     — story/topic prompt
│   ├── MusicAction.hpp           — music intent: prompt / playback / cancel helpers
│   ├── PronunciationFeedbackAction.hpp — drill score + weakest-phoneme feedback + DrillModeToggle
│   └── NoneAction.hpp            — empty transcript guard
├── config/
│   ├── ConfigStore.hpp/cpp       — dotenv loader (env vars take precedence)
│   ├── AppConfig.hpp/cpp         — top-level config container (`ai`, `audio`, `learning`, `pronunciation`, `locale`, `music`)
│   └── ai/AiClientConfig.hpp/cpp — OpenAI-compatible HTTP client settings
└── tts/
    ├── PiperSpeech.hpp/cpp       — thin facade (public C-style API unchanged)
    ├── PiperSampleRate.hpp       — `kPiperSampleRate = 22050` (single source of truth)
    ├── runtime/
    │   └── PiperRuntime.hpp/cpp  — one-time `DYLD_FALLBACK_LIBRARY_PATH` configure (`std::call_once`-guarded)
    ├── wav/
    │   └── WavReader.hpp/cpp     — generic 44-byte WAV reader (no Piper dependency)
    ├── backend/                  — Strategy: synthesise text → int16 PCM
    │   ├── IPiperBackend.hpp          — Strategy interface
    │   ├── PiperSpawn.hpp/cpp         — Template Method: phase helpers (`setup_stdin_stdout_pipes` → `spawn_piper_child` → `pump_stdout` → `reap_child`) over `PipeFdGuard` RAII
    │   ├── PiperWaitStatus.hpp/cpp    — `log_piper_wait_status(int)` shared by pipe backend + streaming play pipeline
    │   ├── PiperPipeBackend.hpp/cpp   — primary: raw PCM over pipes
    │   ├── PiperShellBackend.hpp/cpp  — legacy fallback: `echo | piper --output_file`
    │   └── PiperFallbackBackend.hpp/cpp— composite (primary → fallback) + default factory
    ├── PlayPipeline.hpp/cpp      — `piper_speak_and_play` / `piper_speak_and_play_streaming` implementations (extracted so `PiperSpeech.cpp` can stay a true facade)
    ├── playback/                 — SDL playback policies
    │   ├── SdlAudioDevice.hpp/cpp     — open / close / state (legacy buffered path)
    │   ├── SdlMonoDevice.hpp/cpp      — RAII mono-int16 SDL device used by the streaming player
    │   ├── PcmRingQueue.hpp/cpp       — `std::condition_variable`-backed PCM queue (replaces the old busy-sleep drain in `StreamingSdlPlayer`)
    │   ├── BufferedSdlPlayer.hpp/cpp  — pre-buffered one-shot playback
    │   └── StreamingSdlPlayer.hpp/cpp — thin facade composing `SdlMonoDevice` + `PcmRingQueue`
    └── cli/TextToSpeech.cpp      — text_to_speech executable entry point
```

---

## Design Patterns in Use

The source layout above reflects a handful of explicit GoF patterns that each
pay for themselves by keeping one responsibility per file and letting tests
stub whole subsystems offline. The most load-bearing ones:

### Strategy — pluggable TTS backends (`src/tts/backend/`)

`IPiperBackend` is the interface (`synthesize(text, model) → int16 PCM + sample
rate`). Concrete strategies:

- `PiperPipeBackend` — primary, `posix_spawn` + stdin/stdout pipe, no disk I/O.
- `PiperShellBackend` — legacy fallback, `echo | piper --output_file` + WAV read.
- `PiperFallbackBackend` — composite: try primary, fall back on failure.

`make_default_backend()` wires the default `Pipe → Shell` chain. The TTS
facade (`tts/PiperSpeech.cpp`) just holds an `IPiperBackend` and never knows
which concrete strategy answered — covered by
`tests/test_piper_backend_fallback.cpp` with two stubbed backends.

### Strategy — ingest chunking (`src/learning/ingest/`)

`IChunker` + `make_chunker_for_extension("md" | "jsonl" | …)` route prose to
`ProseChunker` (char-window with overlap) and structured line-oriented data to
`JsonlChunker` (preserves object boundaries). The `Ingestor` coordinator picks
one chunker per file and forwards; covered by
`tests/test_ingest_chunking_strategy.cpp`.

### Template Method — TTS process spawn skeleton (`tts/backend/PiperSpawn.*`)

`PiperSpawn::run_pipe_synth` owns the fixed "spawn → write text → read samples
→ collect stderr" skeleton. Individual backends only fill in the concrete
executable path + args. This eliminates the ~80 LOC of `posix_spawn` +
read/write-pipe boilerplate that used to be duplicated across three functions.

### Template Method — drill scoring pipeline (`pronunciation/drill/DrillScoringPipeline.*`)

Fixed outline: *plan → align → score → intonation → feedback*. Individual
steps take injectable collaborators (`PhonemeModel`, `G2P`, `PronunciationScorer`,
`IntonationScorer`) so tests can supply fakes while the overall shape stays
identical.

### Chain of Responsibility — utterance routing (`voice/UtteranceRouter.*`)

One `route(Utterance)` call walks:

1. `CommandProcessor::match_local` (fast regex, any mode)
2. Drill callback (only while `ListenerMode::Drill`)
3. Tutor callback (only while `ListenerMode::Lesson`)
4. `CommandProcessor::process` (full chat round-trip)

First handler to produce a non-empty `Action` wins. Covered by
`tests/test_utterance_router.cpp` with stub callbacks and a canned
`IHttpClient` behind the fallback.

### Registry / Lookup table — `ActionKind → ActionSideEffectDescriptor`

`voice/ActionSideEffectRegistry.*` keeps a static `descriptor_for(ActionKind)`
table where each row is a `{ModeChange, ListenerMode target, music_hook,
sets_pending_drill}` tuple. `VoiceListener::apply_local_intent_side_effects_`
became a 10-line dispatch over the row instead of an 8-case
`switch(ActionKind)`. Adding a new music / lesson intent is a one-row
extension — there is nowhere left for the listener and the matcher to
disagree on what a phrase means.

### Strategy — pluggable music providers (`src/music/`)

`MusicProvider` is the interface (`search`, `play`, `stop`, `pause`,
`resume`); `YouTubeMusicProvider` is the only concrete strategy today
and has been split into pure command builders (`yt/YtDlpCommands`),
a pure search-output parser (`yt/YtDlpSearch`), and a playback pipeline
(`yt/YtPlaybackPipeline`) that owns the `Subprocess` + `StreamingSdlPlayer`
lifecycle. Adding Apple Music or any other back-end is a new
`MusicProvider` + a row in `MusicFactory::make_provider_from_config`.

### Facade — thin coordinators over split collaborators

`PiperSpeech` keeps the C-style public API (`piper_speak_and_play`, …) while
delegating to `tts/PlayPipeline` + backend / playback strategies;
`EnglishTutorProcessor` keeps its public interface (`process` /
`process_async`) while delegating to `tutor/TutorContextBuilder`,
`tutor/TutorChatRequest`, and `tutor/TutorReplyParser`;
`YouTubeMusicProvider` is now an orchestrator over `music/yt/`;
`StreamingSdlPlayer` composes `SdlMonoDevice` + `PcmRingQueue`;
`PronunciationDrillProcessor`, `VoiceListener`, `Ingestor`, and
`LearningStore` each stayed API-stable while their internals were
broken into single-responsibility files. This is the escape hatch
that lets every refactor land without churning any call site.

### RAII + single-connection invariant

`AudioCapture::MuteGuard`, `WhisperEngine`'s `whisper_context` ownership,
`StatementCache::CachedStmt`, and the SQLite `Transaction` helper all use
RAII to keep "pause/resume", "open/close", and "prepare/finalise" symmetric.
`LearningStore` owns the sole `sqlite3*` and passes it (plus its
`StatementCache&`) to the per-aggregate `hecquin::learning::store::detail`
free functions — explicit argument passing keeps the single-connection
invariant visible in every call site.

---

## Component Details

### AudioCapture

Opens a microphone via SDL2 at 16 kHz, mono, float32. The device is selectable via `AUDIO_DEVICE_INDEX` in `config.env` (`-1` = system default). An SDL audio callback appends samples to a mutex-protected ring buffer. For VAD polling the listener prefers `snapshotRecent(n, out)` which copies only the tail window the RMS gate inspects into a caller-owned buffer (no per-poll allocation); the full `snapshotBuffer()` copy is reserved for the utterance-close path that feeds Whisper and the drill PCM channel. During TTS playback, `pauseDevice()` / `resumeDevice()` prevent mic echo — the "pause → clear → speak → clear → resume" dance is wrapped in an `AudioCapture::MuteGuard` RAII helper used by `speakReply`, the drill announcer callback, and the pronunciation drill entry point. `limitBufferSize()` trims the buffer to keep only the last N seconds.

### VoiceListener

`VoiceListener` is a coordinator over four single-responsibility collaborators:

- `UtteranceCollector` runs the 50 ms poll loop, keeps primary-VAD counters,
  and produces a `CollectedUtterance { pcm, stats }` at silence close.
- `SecondaryVadGate` is the pure `evaluate_secondary_gate(samples, voiced,
  total, cfg)` function (still unit-tested by `test_voice_listener_vad.cpp`).
- `UtteranceRouter` (Chain of Responsibility — see the pattern section above)
  decides which handler runs: local intents → drill callback → tutor callback
  → chat.
- `TtsResponsePlayer` owns the TTS sanitisation regex set, the `MuteGuard`
  wrap, and the actual Piper playback call.

The listener itself just wires these together, owns the `ListenerMode` state
machine, and emits `PipelineEvent`s into the optional sink. The high-level
loop is still:



```
every 50 ms:
    capture.snapshotRecent(vad_window_samples, tail_buf)   # zero-alloc tail copy
    rms     = sqrt(mean(tail_buf²))

    if rms > 0.02 and not collecting:
        collecting = true

    if collecting:
        speech_ms   += 50
        total_frames += 1
        voiced_frames += (has_voice ? 1 : 0)
        silence_ms   = has_voice ? 0 : silence_ms + 50

        if speech_ms >= 500 and silence_ms >= 800:
            samples = capture.snapshotBuffer()             # full copy only on close
            decision = VoiceListener::evaluate_secondary_gate(samples, voiced_frames,
                                                              total_frames, cfg)
            # decision.reason ∈ { accepted, too_quiet, too_sparse, both }
            if not decision.accepted:
                log::info("vad_gate", "outcome=rejected reason=" + decision.reason)
                record_pipeline_event("vad_gate", "skipped", elapsed_ms, attrs)
                clearBuffer(); continue

            transcript = whisper.transcribe(samples)       # may return "" (noise)
            if transcript is not empty:
                action = commands.process(transcript)      # direct synchronous call
                { MuteGuard g(capture);
                  piper_speak_and_play(action.reply);    } # scope resumes mic + clears buffer
            collecting = false

    capture.limitBufferSize(30s max, keep 10s)
```

Configuration (`VoiceListenerConfig`):

| Field | Default | Description |
|---|---|---|
| `vad_window_samples` | 512 | Sample window for RMS calculation |
| `voice_rms_threshold` | 0.02 | Energy threshold to detect speech |
| `min_speech_ms` | 500 | Minimum utterance length to trigger transcription |
| `end_silence_ms` | 800 | Silence duration that marks end of speech |
| `buffer_max_seconds` | 30 | Buffer trim trigger |
| `buffer_keep_seconds` | 10 | Samples kept after trim |
| `min_voiced_frame_ratio` | 0.35 | Minimum fraction of poll frames that must register as voiced during collection; rejects brief noise spikes and music |
| `min_utterance_rms` | 0.015 | Minimum mean RMS over the whole collected utterance; rejects whispers / rustling / faint background chatter |

### WhisperEngine

RAII wrapper around `whisper_context`. Loads a GGML model at construction.
`transcribe()` decodes a float32 sample vector and returns joined segment
text. Behaviour is driven by `WhisperConfig`, populated from
`HECQUIN_WHISPER_*` env vars (see `src/voice/WhisperEngine.hpp`):

| Field              | Env var                         | Default                                        |
|--------------------|---------------------------------|------------------------------------------------|
| `language`         | `HECQUIN_WHISPER_LANGUAGE`      | `en` (falls back to `LocaleConfig::whisper_language`) |
| `n_threads`        | `HECQUIN_WHISPER_THREADS`       | `max(2, hardware_concurrency() / 2)`           |
| `beam_size`        | `HECQUIN_WHISPER_BEAM_SIZE`     | `0` (greedy; set ≥1 to enable beam search)     |
| `no_speech_thold`  | `HECQUIN_WHISPER_NO_SPEECH`     | `0.6`                                          |
| `min_alnum_chars`  | `HECQUIN_WHISPER_MIN_ALNUM`     | `2`                                            |
| `suppress_segs`    | `HECQUIN_WHISPER_SUPPRESS_SEGS` | `0` (leaves per-segment stdout dumps disabled) |

Three layers of noise / hallucination filtering run on the decoder output
before it is handed back to the listener — `transcribe()` returns an empty
string when any gate trips:

1. **Decoder bias** — `suppress_blank`, `suppress_nst` (non-speech tokens),
   `no_speech_thold` (configurable), and `logprob_thold = -1.0` make Whisper
   itself prefer to emit nothing when the audio is silence, music, or static.
2. **Pattern-based annotation stripper** — Whisper emits bracketed
   non-speech markers over noise/music (`[MUSIC]`, `[NOISE]`, `[SOUND]`,
   `[BLANK_AUDIO]`, `[Music playing]`, `(applause)`, `(laughter)` …). A
   single regex `\[[^\]\[]*\]|\([^\)\(]*\)` strips *any* such annotation
   (including future variants) from the decoded text. If the remainder is
   empty — or has fewer than `min_alnum_chars` alphanumeric characters — the
   utterance is treated as noise and `transcribe()` returns an empty string.
3. **No-speech-probability gate** — any segment whose
   `whisper_full_get_segment_no_speech_prob()` exceeds `no_speech_thold`
   causes the whole utterance to be rejected.

### CommandProcessor

A thin façade composing two collaborators, seeded from `AiClientConfig`:

1. **`LocalIntentMatcher`** (`src/ai/LocalIntentMatcher.*`) — regex matching on the
   normalized (lowercase, trimmed) transcript. Patterns come from
   `AppConfig::learning` (`lesson_*_phrases`, `drill_*_phrases`) and the
   matcher's own built-in set for device / story / music intents — the
   single source of truth for phrase lists is `AppConfig`. Returns a
   `std::optional<ActionMatch>` so callers can distinguish "no match" from
   an empty reply; the matched `ActionKind` rides on the result so the
   listener never re-runs the same regex to decide which toggle fired:
   - `turn on|turn off` + `air|switch` → `ActionKind::LocalDevice`
   - `tell me a story` → `ActionKind::InteractionTopicSearch`
   - `open music` → `ActionKind::MusicSearchPrompt` (enters `ListenerMode::Music`)
   - `stop|cancel|exit|close|end music` → `ActionKind::MusicCancel` (aborts in-flight playback + exits `Music` mode)
   - `pause music` → `ActionKind::MusicPause` (best-effort suspend)
   - `continue|resume|unpause music` → `ActionKind::MusicResume`
   - lesson toggles → `ActionKind::LessonModeToggle`
   - drill toggles → `ActionKind::DrillModeToggle`

2. **`ChatClient`** (`src/ai/ChatClient.*`) — remote LLM call against an
   OpenAI-compatible `/chat/completions` endpoint. JSON is built with
   **nlohmann/json**; transport is delegated to an **`IHttpClient`** reference.
   In production it is a `RetryingHttpClient` → `LoggingHttpClient` →
   `CurlHttpClient` chain (backoff for 5xx / 429 / transport failures with
   exponential delay, then telemetry, then libcurl). Tests pass a `FakeHttp`
   directly. On terminal failure (missing API key, permanent transport
   error, non-2xx that exhausted retries, unparseable body) `ask()` returns
   an `ExternalApiAction` carrying a **short, user-friendly spoken reply**
   (mapped per status bucket: auth / not-found / timeout / busy / server /
   parse) while the raw response body, URL, and status code are logged via
   `hecquin::log`. This keeps Piper from reading multi-kilobyte JSON error
   payloads aloud while preserving full diagnostics for operators.

`CommandProcessor::process()` / `process_async()` try the local matcher first,
then fall back to `ChatClient::ask()`. The system prompt is loaded at startup
from `.env/prompts/system_prompt.txt` (editable without recompiling) and stored
in `AiClientConfig::system_prompt`. If the file is missing, a built-in default
is used.

### IHttpClient / HttpClient

`IHttpClient` (`src/ai/IHttpClient.hpp`) is the abstract transport interface
(`post_json`). `CurlHttpClient` is the default implementation; it forwards to
the reusable `http_post_json()` free function in `HttpClient.*`, which owns all
libcurl boilerplate (global init, handle lifecycle, headers, error handling).
Guarded by `#ifdef HECQUIN_WITH_CURL`; `post_json` returns `std::nullopt`
unconditionally when curl is absent. Tests provide a lightweight `FakeHttp`
implementing the same interface — no sockets are opened.

### LoggingHttpClient

A thin **Decorator** over `IHttpClient` (`src/ai/LoggingHttpClient.{hpp,cpp}`)
that captures outbound-call telemetry without touching any call site. It
owns an inner `IHttpClient&` and a `std::function<void(ApiCallRecord)>` sink;
`post_json` forwards to the inner transport, measures wall-clock latency,
records request/response byte counts, and emits one `ApiCallRecord` per call.

```
ChatClient / EmbeddingClient
        │   uses (IHttpClient&)
        ▼
LoggingHttpClient
   ├── delegates ─► CurlHttpClient (libcurl)
   └── sink(ApiCallRecord) ─► LearningStore::record_api_call  [bound in main()]
```

The sink indirection is deliberate: the `ai` library must not depend on
`learning` (that would create a cycle), so the binding happens at the
executable entry points (`EnglishTutorMain.cpp`, `EnglishIngest.cpp`).
Executables that don't want telemetry (or tests that inject a `FakeHttp`)
simply skip the decorator or pass a no-op sink; everything else keeps its
existing contract.

Rows land in the `api_calls` table (see schema below) and are read
(read-only, via SQLite WAL) by the sibling **`dashboard/`** Python / FastAPI
module to render daily traffic, latency, and error-rate charts.

### OpenAiChatContent

`extract_openai_chat_assistant_content(body)` parses an OpenAI-compatible chat
completion response using **nlohmann/json** (permissive / ignore-comments mode)
and returns the first `choices[].message.content` string, or `std::nullopt` on
any structural / type mismatch. No hand-rolled scanning.

### ConfigStore / AppConfig / AiClientConfig

`ConfigStore` parses `.env/config.env` (key=value, `#` comments, `export` prefix, quoted values stripped). Process environment variables always take precedence over file entries.

`AppConfig::load()` accepts an `env_file_path` and an optional `prompts_dir` (both resolved to absolute paths by CMake at compile time to avoid working-directory issues).

`AiClientConfig` resolves settings with the following priority chains:

| Setting | Priority |
|---|---|
| API key | `OPENAI_API_KEY` → `HECQUIN_AI_API_KEY` → `GEMINI_API_KEY` → `GOOGLE_API_KEY` |
| Base URL | `OPENAI_BASE_URL` → `HECQUIN_AI_BASE_URL` (default: `https://api.openai.com/v1`) |
| Model | `HECQUIN_AI_MODEL` → `OPENAI_MODEL` (default: `gpt-4o-mini`) |
| System prompt | Loaded from `<prompts_dir>/system_prompt.txt`; falls back to built-in default |

`ready()` returns true only when libcurl is compiled in and an API key is present.

### PiperSpeech

`src/tts/PiperSpeech.*` is a thin facade over the split layout under `tts/`.
The public C-style entry points (`piper_speak_and_play`,
`piper_speak_and_play_streaming`, `piper_synthesize_to_buffer`) stay intact
so every caller (`VoiceListener::TtsResponsePlayer`,
`DrillReferenceAudio`, the `text_to_speech` CLI) is unchanged. Under the
hood:

- `tts/backend/` — `IPiperBackend` Strategy with `PiperPipeBackend` (primary,
  posix_spawn + pipes), `PiperShellBackend` (legacy shell fallback), and
  `PiperFallbackBackend` (composite); both backends share the
  `PiperSpawn` Template Method so the spawn / write-stdin / read-stdout
  plumbing lives in one place.
- `tts/playback/` — `SdlAudioDevice` (lifecycle), `BufferedSdlPlayer`
  (pre-buffered one-shot), and `StreamingSdlPlayer` (producer/consumer
  ring that starts playback as soon as the first chunk arrives).
- `tts/runtime/PiperRuntime` — one-time `DYLD_FALLBACK_LIBRARY_PATH`
  configure for the bundled Piper shared libraries on macOS.
- `tts/wav/WavReader` — generic 44-byte WAV reader, promoted out of the
  public header so it no longer leaks as part of the Piper API.

Two synthesis backends feed the same streaming SDL2 playback:

```
piper_speak_and_play_streaming(text, model):
    1. synthesise — one of:
         (a) piper_synthesize_to_buffer(text, model) → int16 PCM     (used by drill;
             preferred path when the in-process API is available)
         (b) persistent `piper --stdin` subprocess: pipe text, read a streamed
             WAV on stdout                                            (fallback)
         (c) legacy `echo "text" | piper --output_file /tmp/...wav` shell pipe
             (retained for environments where stdin mode is unavailable)
    2. sdl_play_s16_mono_22k_streaming(pcm_producer)
           — opens SDL2 playback device at 22050 Hz if not already open
           — producer/consumer ring: playback starts as soon as the first
             chunk arrives, so long replies don't stall the mic re-open
           — SDL callback drains chunks until EOF is signalled
```

Path (a) is used for every drill utterance (we already need the raw PCM to
compute the reference pitch contour) and for all cached replays from the
`PronunciationDrillProcessor` LRU (keyed by `std::hash<string>{}(text)`:
8 entries of `{PCM, reference_contour}`). Paths (b)/(c) avoid the
per-utterance shell fork/exec cost on the assistant / tutor reply path.

---

## Action Types

| ActionKind | Produced by | `reply` content |
|---|---|---|
| `None` | Empty transcript | Empty |
| `LocalDevice` | Device regex match | "Okay, turn on/off the …" |
| `InteractionTopicSearch` | Story regex match | User prompt text |
| `MusicSearchPrompt` | `open music` regex match | "What music would you like to play?" — enters `ListenerMode::Music` |
| `MusicPlayback` | `MusicSession` (after yt-dlp resolves the track) | "Now playing …"; restores previous `ListenerMode` and engages the speaker-bleed gate on the VAD collector. Playback runs on a worker thread. |
| `MusicNotFound` | `MusicSession` (search miss / empty query) | "Sorry, I couldn't find that song." — same mode exit as `MusicPlayback` but the bleed gate stays disengaged since no audio is playing. |
| `MusicCancel` | `stop / cancel / exit / close / end music` regex | "Okay, stopping music." — aborts the worker thread via `MusicSession::abort()`. |
| `MusicPause` | `pause music` regex | "Paused." — best-effort `provider.pause()`. |
| `MusicResume` | `continue / resume / unpause music` regex | "Resuming." — counterpart to pause. |
| `ExternalApi` | HTTP API call | API assistant reply |
| `EnglishLesson` | (reserved — lesson-mode cue) | — |
| `GrammarCorrection` | `EnglishTutorProcessor` (Lesson mode) | "You said … / Better … / Reason …" |
| `LessonModeToggle` | Lesson start/end regex match | Short confirmation; flips `ListenerMode` |
| `PronunciationFeedback` | `PronunciationDrillProcessor` | Overall score + weakest word/phoneme + intonation note; next sentence announced after playback |
| `DrillModeToggle` | Drill start/end regex match | Short confirmation; flips `ListenerMode::Drill` |

---

## Threading

| Thread | Role |
|---|---|
| SDL audio callback | Appends mic samples to buffer under mutex |
| Main thread | Polls buffer, runs VAD, calls Whisper, runs `CommandProcessor::process` synchronously, drives TTS |
| Piper persistent subprocess (optional) | Long-lived `piper --stdin` process; consumes text lines, emits WAV on stdout |
| SDL playback callback | Pulls PCM chunks from the streaming ring during playback |

All shared buffer access is guarded by `std::mutex` + `std::lock_guard`.
The earlier `std::async` worker that executed one HTTP call per utterance
has been removed — routing is back to a plain synchronous call because the
phantom future was always immediately joined (see enhancement roadmap,
item 1).

---

## Build System

CMake modular structure under `cmake/`:

| File | Role |
|---|---|
| `project_options.cmake` | C++17, compiler flags |
| `deps_sdl2.cmake` | SDL2 (required) |
| `deps_whisper.cmake` | whisper.cpp (platform-aware search) |
| `deps_piper.cmake` | Piper binary + model path discovery |
| `deps_curl.cmake` | libcurl (optional; sets `HECQUIN_WITH_CURL`) |
| `deps_sqlite_vec.cmake` | SQLite + sqlite-vec (optional; sets `HECQUIN_HAS_SQLITE`) |
| `deps_onnxruntime.cmake` | onnxruntime (optional; sets `HECQUIN_WITH_ONNX`) |
| `dependency_libraries.cmake` | Interface library wrappers |
| `sound_common.cmake` | Internal static lib: `hecquin_common` (Subprocess, ShellEscape, EnvParse, StringUtils, Utf8) — loaded before `piper_speech.cmake` and `sound_libs.cmake` so every higher lib can link it |
| `sound_libs.cmake` | Internal static libs: `hecquin_config`, `hecquin_ai`, `hecquin_voice_pipeline`, `hecquin_music`, `hecquin_learning`, `hecquin_prosody`, `hecquin_pronunciation`, `hecquin_drill` |
| `voice_to_text.cmake` | `voice_detector` executable |
| `text_to_speech.cmake` | `text_to_speech` executable |
| `english_tutor.cmake` | `english_ingest` + `english_tutor` executables |
| `pronunciation_drill.cmake` | `pronunciation_drill` executable |
| `piper_speech.cmake` | `hecquin_piper_speech` static library |
| `sound_tests.cmake` | CTest unit tests — common (`utf8`, `shell_escape`, `subprocess`), config (`config_store`), ai (`openai_chat`, `local_intent`, `net_retry`), voice (`voice_listener_vad`, `noise_floor_tracker`, `music_side_effects`, `whisper_post_filter`, `utterance_router`), tts (`piper_backend_fallback`, `pcm_ring_queue`), music (`music_session`, `yt_dlp_commands`, `yt_dlp_search_parser`), learning (`embedding_json`, `text_chunker`, `learning_store`, `ingest_chunking_strategy`, `content_fingerprint`, `tutor_reply_parser`), pronunciation (`phoneme_vocab`, `ctc_aligner`, `pronunciation_scorer`), prosody (`pitch_tracker`, `intonation_scorer`), drill / tutor (`pronunciation_drill`, `english_tutor_processor`, `drill_sentence_picker`, `drill_reference_audio_lru`) — 30+ tests, run via `ctest --output-on-failure` |

Key preprocessor defines set by CMake:

| Define | Meaning |
|---|---|
| `HECQUIN_WITH_CURL` | libcurl found; external API enabled |
| `HECQUIN_HAS_SQLITE` | SQLite + sqlite-vec available; learning / drill schema enabled |
| `HECQUIN_WITH_ONNX` | onnxruntime found; wav2vec2 pronunciation scoring enabled |
| `DEFAULT_MODEL_PATH` | Whisper GGML model path |
| `DEFAULT_PIPER_MODEL_PATH` | Piper voice model path |
| `DEFAULT_PRONUNCIATION_MODEL_PATH` | wav2vec2 phoneme-CTC ONNX model path |
| `DEFAULT_PRONUNCIATION_VOCAB_PATH` | vocab.json path for the phoneme model |
| `DEFAULT_CONFIG_PATH` | Absolute path to `.env/config.env` |
| `DEFAULT_PROMPTS_DIR` | Absolute path to `.env/prompts/` |
| `PIPER_EXECUTABLE` | Path to piper binary |

### Platform Detection

| Platform | Build prefix |
|---|---|
| macOS (Darwin) | `.env/mac/`, `build/mac/` |
| Raspberry Pi (aarch64/armv7l) | `.env/rpi/`, `build/rpi/` |
| Linux x86_64 | `.env/linux/`, `build/linux/` |

Whisper/Piper models are shared across platforms in `.env/shared/models/`. Default model
paths (Whisper GGML, Piper voice, config file, system prompt) and the `dev.sh` command
cheat sheet are documented in [`README.md`](./README.md).

---

## Music Subsystem

`src/music/` turns "open music" into a two-turn conversation:

1. `LocalIntentMatcher` resolves `open music` → `MusicAction::prompt`. The
   listener switches to `ListenerMode::Music` via
   `apply_local_intent_side_effects_` and TTS speaks *"What music would you
   like to play?"*.
2. The next utterance arrives in `Music` mode. `UtteranceRouter` forwards
   the transcript to `MusicCallback` (the Music mode branch runs before
   Drill / Lesson so a user in an active learning session can still detour
   into music). The callback lives in `MusicSession::handle`, which asks
   the injected `MusicProvider` to search synchronously and — on a hit —
   dispatches `provider.play()` to a private worker thread. `handle()`
   returns a `MusicPlayback` action on a hit, or `MusicNotFound` on a
   miss / empty query, immediately so the listener drops back into
   `home_mode_` and keeps capturing voice while the song streams.
3. Mid-song, the listener still feeds every utterance through
   `LocalIntentMatcher` first.  `stop / cancel / exit / close / end
   music` resolves to `MusicAction::cancel` (`ActionKind::MusicCancel`);
   the listener's `voice/MusicSideEffects` collaborator invokes
   `MusicSession::abort` → `provider.stop()`, joining the worker thread
   before the cancel TTS reply is spoken so the apology doesn't fight
   the song for the speakers.  `pause music` and `continue / resume /
   unpause music` similarly map to `MusicAction::pause / resume`,
   forwarded through best-effort `MusicProvider::pause / resume`
   methods (the default `YouTubeMusicProvider` toggles its SDL device).
   The same collaborator owns the `set_external_audio_active` /
   `reset_noise_floor` toggles on the VAD collector so speaker bleed
   never poisons the adaptive noise floor; see `voice/MusicSideEffects.hpp`.

**Providers (`MusicProvider` interface).** Extensible by design:

- **`YouTubeMusicProvider`** (default) — shells out to `yt-dlp` for both
  search (`ytsearch1:<query> music`) and playback. Playback pipes
  `yt-dlp -f bestaudio -o - | ffmpeg -f s16le -ac 1 -ar <rate>` into the
  existing `StreamingSdlPlayer`, so the entire audio path reuses the SDL2
  producer / consumer ring already used for Piper TTS. Auth is optional:
  if `HECQUIN_YT_COOKIES_FILE` points at a Netscape-format cookies file
  exported from a browser signed into YouTube Premium, both subprocesses
  receive `--cookies <path>` for ad-free + higher-bitrate streams.
- **Apple Music (future)** — plugs in as another `MusicProvider` with no
  changes to the listener, matcher, router, or session.

`MusicFactory::make_provider_from_config` selects the back-end from
`AppConfig::music::provider` (default `"youtube"`). Unknown names fall
back to YouTube with a warning so a typo can't silently disable music.

**Configuration** (all optional; defaults match a macOS / Raspberry Pi
Homebrew / apt install):

- `HECQUIN_MUSIC_PROVIDER`
- `HECQUIN_YT_COOKIES_FILE`
- `HECQUIN_YT_DLP_BIN`, `HECQUIN_FFMPEG_BIN`
- `HECQUIN_MUSIC_SAMPLE_RATE`

**Cancellation.** `cancel music` / `stop music` / `exit music` are part of
the built-in regex alternations, so the user can bail out before
supplying a song name. Mid-playback cancellation via voice is blocked by
the `MuteGuard` (documented limitation); SIGINT / shutdown cleanly kills
the `yt-dlp | ffmpeg` subprocess and finalises the SDL player.

---

## English Tutor Subsystem

Built on the same voice/TTS pipeline, the tutor adds:

- A local **SQLite + sqlite-vec** vector DB for curriculum and progress.
- A **Gemini embeddings** client (`gemini-embedding-001`, with a `dimensions` override so the on-disk vec0 schema stays at 768) reusing `HttpClient`.
- A second command processor (`EnglishTutorProcessor`) that does RAG → chat → correction.
- A second executable `english_tutor` that forces the listener into `Lesson` mode.
- A third executable `english_ingest` (no SDL2/Whisper) for offline curriculum ingestion.

### Mode switching

`VoiceListener` keeps a `ListenerMode { Assistant, Lesson }`. Every cycle it
runs the transcript through `LocalIntentMatcher::match()` first — that returns
an `ActionMatch{ ActionKind::LessonModeToggle, … }` on phrases like
`start english lesson` / `exit lesson`, which flips the mode without hitting
the network. When `mode == Lesson`, transcripts are handed to the
`TutorCallback` (a `std::function<Action(const std::string&)>`) instead of the
external chat API.

### Data flow (Lesson mode)

```
transcript
    │
    ▼
LocalIntentMatcher.match       ← fast regex toggles (lesson on/off, device…)
    │  (no match)
    ▼
EnglishTutorProcessor.process
    ├── RetrievalService.top_k(transcript, k=3)
    │        └── EmbeddingClient.embed → LearningStore.query_top_k (vec0)
    ├── build_chat_body(user + RAG context)       [nlohmann/json]
    ├── IHttpClient.post_json(chat_completions_url)
    ├── extract_openai_chat_assistant_content     [nlohmann/json]
    ├── parse_tutor_reply → GrammarCorrectionAction
    └── ProgressTracker.log_interaction → interactions + vocab_progress
    │
    ▼
GrammarCorrectionAction.to_reply() → TTS
```

### SQLite schema (learning DB, v3)

`LearningStore::kSchemaVersion == 3`. All migrations are idempotent
(`CREATE … IF NOT EXISTS`), stamped into `kv_metadata(schema_version)` on the
very first open and never downgraded afterwards. The table cluster is written
by different translation units under `src/learning/store/` — all sharing the
same class and DB file.

Hot-path statements are cached per connection in a `detail::StatementCache`
(keyed by opaque tag such as `"topk.vec"`, `"pipeline_events.record"`): every
call hits `sqlite3_reset` + rebind instead of re-preparing the SQL text,
which is ~10× cheaper for these short queries.

| Table                     | Written by (`src/learning/store/…`)                              | Columns                                                   |
|---------------------------|------------------------------------------------------------------|-----------------------------------------------------------|
| `kv_metadata`             | `LearningStore.cpp`                                              | `key PRIMARY KEY, value` (schema_version, embedding_dim)  |
| `documents`               | `LearningStoreDocuments.cpp`                                     | `id, source, kind, title, body, metadata_json, created_at` |
| `vec_documents`           | `LearningStoreDocuments.cpp`                                     | `USING vec0(embedding FLOAT[768])` (or BLOB fallback)     |
| `ingested_files`          | `LearningStoreDocuments.cpp`                                     | `path PRIMARY KEY, hash, ingested_at`                     |
| `sessions`                | `LearningStoreSessions.cpp`                                      | `id, mode, started_at, ended_at`                          |
| `interactions`            | `LearningStoreSessions.cpp`                                      | `id, session_id, user_text, corrected_text, grammar_notes, created_at` |
| `vocab_progress`          | `LearningStoreSessions.cpp`                                      | `word PRIMARY KEY, first_seen_at, last_seen_at, seen_count, mastery` |
| `pronunciation_attempts`  | `LearningStorePronunciation.cpp`                                 | `id, session_id, reference, transcript, pron_overall, intonation_overall, per_phoneme_json, created_at` |
| `phoneme_mastery`         | `LearningStorePronunciation.cpp`                                 | `ipa PRIMARY KEY, attempts, avg_score, last_seen_at`      |
| `api_calls` **(v2)**      | `LearningStoreApiCalls.cpp` (via `LoggingHttpClient` sink)       | `id, ts, provider, endpoint, method, status, latency_ms, request_bytes, response_bytes, ok, error` |
| `request_logs` **(v2)**   | Python `dashboard/` middleware                                   | `id, ts, path, method, status, latency_ms, remote_ip, user_agent` |
| `pipeline_events` **(v3)**| `LearningStoreApiCalls.cpp` (shared `ApiCallSink` mechanism)     | `id, ts, event, outcome, duration_ms, attrs_json` — VAD gate decisions, Whisper latency + p(no_speech), drill alignment ok/fail, Piper synth ms |

Indexes on `api_calls(ts)`, `api_calls(provider, ts)`, `request_logs(ts)`,
`pipeline_events(ts)`, and `pipeline_events(event, ts)` keep the dashboard's
per-day aggregations cheap.

`pipeline_events` is append-only and intentionally schemaless in its
`attrs_json` payload: a new metric can be added (e.g. `drill_alignment.ok=1,
segments=42`) without another migration.

If `sqlite-vec` cannot be downloaded at configure time, `LearningStore` transparently
falls back to a BLOB-backed table and a C++ brute-force cosine scan — the rest of the
pipeline works unchanged (just slower on large curricula).

### API-call telemetry pipeline (v2)

```
EnglishTutorMain / EnglishIngest
    │
    ├── construct CurlHttpClient                                (transport)
    ├── construct ApiCallSink = &LearningStore::record_api_call (binding site)
    ├── construct LoggingHttpClient(inner, sink, provider="chat" | "embedding")
    │
    ▼
ChatClient / EmbeddingClient  (take IHttpClient& — unchanged)
    │
    ▼
LoggingHttpClient::post_json
    │
    ├── now()                                       → t0
    ├── inner_.post_json(url, headers, body)        → {status, response}
    ├── now() − t0                                  → latency_ms
    ├── sink_(ApiCallRecord{provider, url, …})
    ▼
LearningStoreApiCalls.cpp → INSERT INTO api_calls …  (SQLite WAL mode)
    │
    ▼  [read-only, via WAL]
dashboard/ (FastAPI + SQLite)  → charts, daily aggregates, error-rate panels
```

Because every cross-library edge is a reference / callback / plain SQL write,
the `ai` library never includes anything from `learning`, and `learning` never
includes anything from `ai` — the dependency graph remains acyclic, and the
decorator can be omitted entirely in test builds or on voice-only executables
that don't care about telemetry.

### Ingest pipeline

`scripts/fetch_curriculum.sh` pulls public datasets into
`.env/shared/learning/curriculum/{vocabulary,grammar,dictionary,readers}/`. The
`english_ingest` binary then runs the `Ingestor` coordinator, which delegates
to the per-responsibility helpers under `src/learning/ingest/`:

1. `FileDiscovery::collect_files` lists every text file under `curriculum/`
   and `custom/`, tagging each with a `kind` and filtering by extension.
2. `ContentFingerprint` computes an FNV-1a-based hash; already-ingested files
   (via `LearningStore::is_file_already_ingested`) are skipped unless
   `--rebuild` is passed.
3. `make_chunker_for_extension` (Strategy) picks `JsonlChunker` for jsonl/json
   and `ProseChunker` otherwise. Prose splits into ~1800-char chunks with a
   200-char overlap; JSONL preserves object boundaries.
4. `EmbeddingBatcher` batches chunks through `EmbeddingClient::embed_many`
   with per-chunk single-embed fallback for partial failures.
5. `DocumentPersister` builds each `DocumentRecord` and upserts `documents`
   + `vec_documents`; `ProgressReporter` prints per-file progress and ETA.
6. `LearningStore::record_ingested_file` writes the file hash on success.

### Configuration additions

`AiClientConfig` grows `embeddings_url`, `embedding_model`, `embedding_dim`, and
`tutor_system_prompt`. `AppConfig` gains a `LearningConfig` block that reads the
`HECQUIN_LEARNING_*` env variables (see README for the full table).

---

## Pronunciation & Intonation Drill Subsystem

The drill shares the voice capture / VAD / whisper transcription path with the
assistant but adds a **third listener mode** (`ListenerMode::Drill`), a raw-PCM
channel from the listener, and a fully-local scoring stack. No cloud call is
made on the acoustic path.

### Mode switching & data plumbing

`VoiceListener` now carries two parallel callbacks — `TutorCallback` (Lesson
mode) and `DrillCallback` (Drill mode) — and, because drill scoring needs the
raw acoustic signal, the callbacks receive an `Utterance { transcript, pcm_16k
}` rather than a bare string. `LocalIntentMatcher` gained
`DrillModeToggleAction` pattern groups that fire the same way
`LessonModeToggle` does, and the listener exposes `setDrillAnnounceCallback()`
so the drill processor can queue the next sentence after any feedback TTS has
finished playing.

### Scoring pipeline

```
reference sentence (sampled by PronunciationDrillProcessor)
    │
    ├── Piper TTS ──► piper_synthesize_to_buffer ──► 22050 Hz int16 PCM
    │                       │
    │                       └── PitchTracker (22050 Hz config) ──► reference F0 contour
    │                       └── SDL2 playback to speaker
    │
user speech (captured in Drill mode)
    │
    ├── WhisperEngine ──► transcript (for logging / weak-word heuristics)
    │
    └── raw 16 kHz float32 PCM
              │
              ├── PhonemeModel (ONNX, wav2vec2 phoneme-CTC)
              │     ├── Emissions (log-softmax, ~20 ms stride)
              │     └── G2P.generate(reference) ──► target phoneme ids
              │           └── espeak-ng --ipa=3 → PhonemeVocab::tokenize_ipa
              │
              ├── CtcAligner.align(emissions, targets) ──► per-phoneme frame spans
              ├── PronunciationScorer.score(plan, alignment) ──► GOP → 0..100
              └── PitchTracker (16 kHz config) ──► user F0 contour
                         │
                         └── IntonationScorer.score(reference, user)
                                    └── DTW on semitones (median-normalised)
                                    └── final-phrase direction check
```

The resulting `PronunciationFeedbackAction` carries the overall score, the
weakest `PronunciationDrillConfig::max_feedback_words` words + phonemes (2 by
default), an intonation note, and a flag for the "next sentence"
announcement. After `VoiceListener` speaks the feedback, it invokes the queued
`drill_announce_cb_` to play the next target.

#### Sentence picker (spaced repetition)

`PronunciationDrillProcessor::next_sentence_` is no longer a one-shot shuffle.
At load time the processor runs G2P over the whole pool once and builds a
phoneme → sentence-ids index. Each pick queries
`LearningStore::weakest_phonemes(n)` — a small window on `phoneme_mastery`
(lowest `avg_score`, then oldest `last_seen_at`) — and biases the next draw
toward sentences whose IPA plan contains those phonemes, with a small epsilon
chance of a plain rotation so the learner still sees variety.

#### Reference contour / PCM cache

The reference PCM + YIN pitch contour for each sentence is memoised in a
tiny LRU keyed by `std::hash<std::string>{}(text)` (8 entries). Repeats and
rotate-backs replay cached audio without re-shelling Piper or re-running YIN,
saving ~200–500 ms per repeat.

#### Per-phoneme score calibration

`PronunciationScorerConfig::per_phoneme` is an IPA → `{min_logp, max_logp}`
map that overrides the global 0..100 anchors when a phoneme is present.
Nasals and fricatives routinely post lower log-posteriors even when
articulated correctly, and a single global floor under-scores them. Overrides
are loaded lazily from `.env/shared/models/pronunciation/calibration.json`
(path configurable via `HECQUIN_PRONUNCIATION_CALIBRATION`); a missing or
malformed file is silently ignored.

### Data structures (see `src/learning/pronunciation/PhonemeTypes.hpp`)

| Type                | Role                                                           |
|---------------------|----------------------------------------------------------------|
| `PhonemeToken`      | `{id, ipa}` — one phoneme in model-vocab space                  |
| `WordPhonemes`      | `{word, phonemes}` — G2P output for a single word               |
| `G2PResult`         | Whole-utterance G2P with `flat_ids()` helper                    |
| `Emissions`         | `{logits[T][V], frame_stride_ms, blank_id}` from wav2vec2       |
| `AlignSegment`      | `{phoneme_id, start_frame, end_frame, log_posterior}`           |
| `AlignResult`       | `{segments, ok}` from forced alignment                          |
| `PhonemeScore`      | `{ipa, score_0_100, start_frame, end_frame}`                    |
| `WordScore`         | Aggregates `phonemes` into a word-level score                   |
| `PronunciationScore`| Overall + per-word breakdown                                    |

### Progress schema additions

The drill writes to `pronunciation_attempts` and `phoneme_mastery` — both now
part of the shared v3 schema (see **English Tutor Subsystem → SQLite schema
(learning DB, v3)** for the exhaustive column list). Implementation lives in
`LearningStorePronunciation.cpp`, so adding a new column only touches that one
translation unit and `LearningStoreMigrations.cpp`.

`ProgressTracker::log_pronunciation()` writes both tables atomically in one
transaction so per-phoneme mastery stays consistent with the raw attempts log.

### Optional onnxruntime

The pronunciation module links against the `hecquin_deps_onnxruntime` interface
target regardless of whether onnxruntime was actually found on the system. When
found, CMake defines `HECQUIN_WITH_ONNX=1`; `PhonemeModel::load()` opens the
ONNX session and `infer()` returns real emissions. Otherwise `PhonemeModel` is
compiled as a no-op stub, `load()` returns false, and
`PronunciationDrillProcessor` reports "pronunciation scoring unavailable" in
TTS while still exercising the intonation path (which has no external deps).

### Configuration

All `HECQUIN_DRILL_*`, `HECQUIN_PRONUNCIATION_*`, and `HECQUIN_ONNX_PROVIDER` keys
are documented in [`README.md → English Tutor Mode → Configuration knobs`](./README.md#configuration-knobs-envconfigenv).
`PronunciationDrillProcessor` reads these through `AppConfig` at startup — no runtime
reconfiguration.

---

## Observability

The sound module uses a single in-process structured logger at
`src/observability/Log.hpp` — `hecquin::log::info/warn/error(tag, fmt, …)`
and `hecquin::log::kv(level, tag, msg, {kv…})`. Output is pretty one-line
by default and switches to JSON Lines when `HECQUIN_LOG_FORMAT=json`, so the
dashboard or a log-scraper can ingest without regex gymnastics. Level is
controlled by `HECQUIN_LOG_LEVEL` (`debug|info|warn|error`, default `info`).

On top of logs, stage-level latencies and outcomes are recorded into the
`pipeline_events` table (see schema section) via the same `ApiCallSink`
mechanism the HTTP decorator uses:

| `event`          | Written by                                | `outcome`       | `attrs_json` highlights                                     |
|------------------|-------------------------------------------|-----------------|-------------------------------------------------------------|
| `vad_gate`       | `VoiceListener::run`                      | `ok` / `skipped`| `reason` (`accepted`, `too_quiet`, `too_sparse`, `both`), `rms`, `voiced_ratio` |
| `whisper`        | `WhisperEngine::transcribe`               | `ok` / `error`  | `latency_ms`, `no_speech`, `chars`                          |
| `piper_synth`    | `PiperSpeech` synth paths                 | `ok` / `error`  | `backend` (`buffer` / `stdin` / `shell`), `latency_ms`, `bytes` |
| `drill_align`    | `PronunciationDrillProcessor::score`      | `ok` / `skipped`| `segments`, `phonemes`, `overall_0_100`                     |
| `drill_pick`     | `PronunciationDrillProcessor::pick_and_announce` | `ok`     | `source` (`spaced_repetition` / `rotation`), `target_phoneme` |

Because the sink is bound at the executable entry points (same pattern as
`api_calls`), voice-only executables and unit tests can skip the sink and
the rest of the pipeline keeps its existing contract.

---

## Multi-locale plumbing

`AppConfig::locale` (`LocaleConfig`) exposes three locale strings:

| Field              | Consumed by                                             | Env override              | Default   |
|--------------------|---------------------------------------------------------|---------------------------|-----------|
| `ui`               | Prompt file lookup (`<prompts_dir>/<ui>/…`), fallback to default | `HECQUIN_LOCALE`     | `en-US`   |
| `whisper_language` | `WhisperEngine::transcribe` (whisper `language` param)  | `HECQUIN_WHISPER_LANGUAGE`| `en`      |
| `espeak_voice`     | `G2P::phonemize` (`espeak-ng --voice=…`)                | `HECQUIN_ESPEAK_VOICE`    | `en-us`   |

All three default to English and current builds are bit-for-bit compatible
with the previous locale-free behaviour; the plumbing is there so a future
non-English voice stack can be wired without touching every subsystem.

---

## Testing

The test suite is a set of small assertion-based `main()` programs under
`sound/tests/` — no gtest / Catch2 dependency. Each binary is registered
through `hecquin_add_unit_test` in `sound/cmake/sound_tests.cmake`, links
directly against the narrow static libs in `sound/cmake/sound_libs.cmake`
(so the same `.cpp` is never recompiled per test), and is driven by
`ctest --output-on-failure` after a normal build.

Tests that need SQLite are guarded by `if (HECQUIN_HAS_SQLITE)` so the
suite still runs on minimal build boxes.

### What each test covers

| Area | Test | What it asserts |
|---|---|---|
| **AI / HTTP** | `test_openai_chat_content` | `choices[0].message.content` extraction round-trip on canned OpenAI / Gemini-compat JSON. |
| | `test_local_intent_matcher` | Full regex matrix for device / story / music / lesson / drill intents; enable-bit is set correctly on toggles. |
| | `test_retrying_http` | Exponential backoff + transient-classification (5xx, 429, transport) without linking libcurl / libssl. |
| **Voice** | `test_voice_listener_vad` | `SecondaryVadGate::evaluate_secondary_gate` reasons — `too_quiet`, `too_sparse`, `both`, `accepted`. |
| | `test_utterance_router` | Chain of Responsibility ordering: local-intent → drill → tutor → chat fallback, plus null-callback safety. Uses a stub `IHttpClient` for the fallback branch. |
| **TTS** | `test_piper_backend_fallback` | `PiperFallbackBackend` strategy composition: primary-wins, fallback-wins, both-fail (outputs cleared), null-primary safety. No real Piper. |
| **Learning / store** | `test_learning_store` | SQLite open + schema migration + document upsert + vector retrieval round-trip. |
| | `test_config_store` | `.env` parsing, env-var overrides, quoted values. |
| | `test_utf8` | `sanitize_utf8` preserves valid multi-byte UTF-8, drops CP-1252 0xA0, replaces overlong sequences with U+FFFD. |
| **Learning / ingest** | `test_embedding_client_json` | `EmbeddingClient` ↔ fake HTTP round-trip (batch + single). |
| | `test_text_chunker` | Prose chunker boundary behaviour (budget, overlap, whitespace preference). |
| | `test_ingest_chunking_strategy` | `make_chunker_for_extension` dispatch (jsonl/json → line-based; prose → char-window) + per-chunker invariants. |
| | `test_content_fingerprint` | FNV-1a determinism + whitespace / byte / length sensitivity. |
| **Pronunciation** | `test_phoneme_vocab` | Greedy longest-match IPA → id tokenisation. |
| | `test_ctc_aligner` | Viterbi forced alignment on a hand-crafted trellis; backpointer correctness + partial-alignment fallback. |
| | `test_pronunciation_scorer` | logp → 0..100 mapping, per-phoneme / word / overall aggregation, calibration anchors. |
| **Prosody** | `test_pitch_tracker` | YIN on synthetic sine waves; voiced vs unvoiced frame classification. |
| | `test_intonation_scorer` | Semitone DTW + final-direction rule (rising / falling / flat) across reference vs candidate contours. |
| **Drill / tutor** | `test_pronunciation_drill` | End-to-end drill processor with fake emissions + injected plan (no espeak-ng / onnxruntime / Piper). |
| | `test_drill_sentence_picker` | Round-robin picker when no store / G2P; `load()` resets cursor; empty-pool safety. |
| | `test_drill_reference_audio_lru` | LRU insert / lookup / eviction, MRU bump on hit, `cache_size == 0` disables. Exercised through `*_for_test` hooks so Piper / SDL never run. |
| | `test_english_tutor_processor` | RAG context truncation + three-line reply parsing; `short_reply_for_status` wiring on non-2xx responses. Real `LearningStore` in `/tmp`, fake HTTP for both embed + chat. |

### Conventions

- Every new file that represents a behavioural unit gets a matching
  `tests/test_*.cpp`. Keep the assertion style in line with `test_utf8.cpp`
  (free `expect_*` helpers, `std::exit(1)` on first mismatch, all-passed
  log at the end).
- Tests must run offline. For anything that would otherwise touch network
  or spawn a subprocess, inject an `IHttpClient` / `IPiperBackend` stub or
  reach for the `*_for_test` hook on the collaborator.
- Heavy dependencies (SQLite, Piper, onnxruntime) are gated behind
  `HECQUIN_HAS_SQLITE` etc. so the smaller tests still build and pass in
  environments without those packages.

---

## CI, formatting, and hooks

- `.github/workflows/sound.yml` runs `build-and-test` on `ubuntu-latest` and `macos-latest` (system deps → whisper.cpp clone + build → CMake configure → build → `ctest --output-on-failure`). Build logs are uploaded as an artefact on failure. A companion `lint` job runs `clang-format --dry-run` on changed files (blocking) and `clang-tidy` on the full `src/` tree (report-only, `continue-on-error: true`, ratcheting planned).
- `sound/.clang-format` is a Google-flavoured style with 100-column lines, 4-space indent, left-aligned pointers, and include sorting disabled so existing include groups aren't reshuffled.
- `sound/.clang-tidy` enables a conservative set of `bugprone-*`, `cert-*`, `clang-analyzer-*`, `modernize-*`, `performance-*`, and `readability-*` checks (with the noisiest ones disabled) and reuses `.clang-format` for fix formatting.
- `sound/scripts/pre-commit.sh` runs `clang-format -i` on staged `*.{c,cc,cpp,h,hpp}` paths under `sound/` and re-stages them. Install via `./dev.sh hooks:install` (symlinks the script into `.git/hooks/pre-commit`).

---

## CLI bootstrap (`LearningApp`)

`EnglishTutorMain.cpp` and `PronunciationDrillMain.cpp` both delegate their
shared setup — `VoiceApp` init, pronunciation path resolution,
`LearningStore` open + migrations, `ProgressTracker` wiring, base
`PronunciationDrillProcessor` construction, `LocalIntentMatcher` config, and
`pipeline_events` sink binding — to a single `hecquin::learning::cli::LearningApp`
helper. Only the mode-specific glue stays in each `main()`:

- `english_tutor` installs a `TutorCallback` backed by `EnglishTutorProcessor`.
- `pronunciation_drill` forces the listener into `Drill` mode and installs a `DrillCallback`.
