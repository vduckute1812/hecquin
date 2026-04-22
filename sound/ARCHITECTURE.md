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
    ▼
WhisperEngine — one-shot greedy transcription (English)
    │
    ▼
CommandProcessor
    ├── Local regex matching (no network, instant)
    │       "turn on/off" + device  → DeviceAction
    │       "tell me a story"       → TopicSearchAction
    │       "open music"            → MusicAction
    │
    └── External API fallback (async, libcurl)
            POST /v1/chat/completions → ExternalApiAction
    │
    ▼
PiperSpeech — synthesize WAV via subprocess → SDL2 playback
    │
    ▼
Speaker
```

---

## Source Layout

```
src/
├── voice/
│   ├── AudioCapture.hpp/cpp      — SDL2 microphone capture
│   ├── AudioCaptureConfig.hpp    — POD config (no SDL include)
│   ├── WhisperEngine.hpp/cpp     — whisper.cpp inference wrapper
│   ├── VoiceListener.hpp/cpp     — VAD + pipeline orchestration
│   ├── VoiceApp.hpp/cpp          — shared bootstrap for voice executables
│   └── VoiceDetector.cpp         — voice_detector executable entry point
├── ai/
│   ├── IHttpClient.hpp           — abstract HTTP transport (testable)
│   ├── HttpClient.hpp/cpp        — libcurl `http_post_json` + `CurlHttpClient`
│   ├── LoggingHttpClient.hpp/cpp — decorator: IHttpClient → ApiCallSink telemetry
│   ├── LocalIntentMatcher.hpp/cpp— regex-based local intents
│   ├── ChatClient.hpp/cpp        — remote LLM client (IHttpClient injected)
│   ├── CommandProcessor.hpp/cpp  — façade: matcher + chat
│   └── OpenAiChatContent.hpp/cpp — nlohmann/json response extractor
├── common/
│   └── StringUtils.hpp           — header-only trim/lower/starts_with
├── learning/
│   ├── store/                             — SQLite-backed persistence (one class, split impl)
│   │   ├── LearningStore.hpp              — Public API (one class)
│   │   ├── LearningStore.cpp              — lifecycle + metadata (open/close, kv)
│   │   ├── LearningStoreMigrations.cpp    — all DDL (schema v2)
│   │   ├── LearningStoreDocuments.cpp     — documents + ingested_files + drill pool
│   │   ├── LearningStoreVectorSearch.cpp  — query_top_k (vec0 + BLOB fallback)
│   │   ├── LearningStoreSessions.cpp      — sessions + interactions + vocab
│   │   ├── LearningStorePronunciation.cpp — pronunciation_attempts + phoneme_mastery
│   │   ├── LearningStoreApiCalls.cpp      — api_calls (written by LoggingHttpClient)
│   │   └── internal/SqliteHelpers.hpp     — private RAII glue (StmtGuard, Transaction, prepare_or_log, …)
│   ├── EmbeddingClient.hpp/cpp   — Gemini embeddings (OpenAI-compat, batched)
│   ├── Ingestor.hpp/cpp          — curriculum → chunks → embeddings
│   ├── TextChunker.hpp/cpp       — standalone chunking utility
│   ├── RetrievalService.hpp/cpp  — vector search helpers
│   ├── ProgressTracker.hpp/cpp   — per-user learning log (grammar + pronunciation)
│   ├── EnglishTutorProcessor.*   — RAG + grammar correction pipeline
│   ├── PronunciationDrillProcessor.hpp/cpp — sentence picker + scoring orchestrator
│   ├── pronunciation/            — wav2vec2 + CTC forced alignment + GOP
│   │   ├── PhonemeTypes.hpp      — shared data structs (Emissions, AlignSegment, …)
│   │   ├── PhonemeVocab.hpp/cpp  — IPA ↔ id tokenizer (greedy longest-match)
│   │   ├── PhonemeModel.hpp/cpp  — ONNX Runtime wrapper (optional: HECQUIN_WITH_ONNX)
│   │   ├── G2P.hpp/cpp           — espeak-ng --ipa=3 → target phoneme ids
│   │   ├── CtcAligner.hpp/cpp    — Viterbi forced alignment
│   │   └── PronunciationScorer.hpp/cpp — logp → 0..100 per phoneme / word / overall
│   ├── prosody/                  — local intonation pipeline
│   │   ├── PitchTracker.hpp/cpp  — YIN F0 contour + per-frame RMS
│   │   └── IntonationScorer.hpp/cpp — semitone DTW + final-direction rule
│   └── cli/                      — `english_ingest`, `english_tutor`, `pronunciation_drill` entries
├── actions/
│   ├── ActionKind.hpp            — enum: None / LocalDevice / TopicSearch / Music / ExternalApi / AssistantSdk / GrammarCorrection / LessonModeToggle / PronunciationFeedback / DrillModeToggle
│   ├── Action.hpp                — {kind, reply, transcript}
│   ├── DeviceAction.hpp          — power control confirmation text
│   ├── ExternalApiAction.hpp     — wraps API response
│   ├── TopicSearchAction.hpp     — story/topic prompt
│   ├── MusicAction.hpp           — music intent
│   ├── PronunciationFeedbackAction.hpp — drill score + weakest-phoneme feedback + DrillModeToggle
│   └── NoneAction.hpp            — empty transcript guard
├── config/
│   ├── ConfigStore.hpp/cpp       — dotenv loader (env vars take precedence)
│   ├── AppConfig.hpp/cpp         — top-level config container
│   └── ai/AiClientConfig.hpp/cpp — OpenAI-compatible HTTP client settings
└── tts/
    ├── PiperSpeech.hpp/cpp       — synthesize + play pipeline
    └── cli/TextToSpeech.cpp      — text_to_speech executable entry point
```

---

## Component Details

### AudioCapture

Opens a microphone via SDL2 at 16 kHz, mono, float32. The device is selectable via `AUDIO_DEVICE_INDEX` in `config.env` (`-1` = system default). An SDL audio callback appends samples to a mutex-protected ring buffer. The main thread calls `snapshotBuffer()` for a thread-safe copy. During TTS playback, `pauseDevice()` / `resumeDevice()` prevent mic echo. `limitBufferSize()` trims the buffer to keep only the last N seconds.

### VoiceListener

The main listen loop:

```
every 50 ms:
    samples = capture.snapshotBuffer()
    rms     = sqrt(mean(samples[-512:]²))

    if rms > 0.02 and not collecting:
        collecting = true

    if collecting:
        speech_ms  += 50
        silence_ms  = has_voice ? 0 : silence_ms + 50

        if speech_ms >= 500 and silence_ms >= 800:
            transcript = whisper.transcribe(samples)
            future     = commands.process_async(transcript)
            action     = future.get()
            capture.pauseDevice()
            piper_speak_and_play(action.reply)
            capture.resumeDevice()
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

### WhisperEngine

RAII wrapper around `whisper_context`. Loads a GGML model at construction. `transcribe()` runs greedy decoding on a float32 sample vector and returns joined segment text. Language is hard-coded to English. Known Whisper noise tokens (`[BLANK_AUDIO]`, `[NO_SPEECH]`, `[ Inaudible Remark ]`, etc.) are filtered out — `transcribe()` returns an empty string for these.

### CommandProcessor

A thin façade composing two collaborators, seeded from `AiClientConfig`:

1. **`LocalIntentMatcher`** (`src/ai/LocalIntentMatcher.*`) — regex matching on the
   normalized (lowercase, trimmed) transcript. Returns a `std::optional<ActionMatch>`
   so callers can distinguish "no match" from an empty reply:
   - `turn on|turn off` + `air|switch` → `ActionKind::LocalDevice`
   - `tell me a story` → `ActionKind::InteractionTopicSearch`
   - `open music` → `ActionKind::InteractionMusicSearch`
   - lesson-mode toggles → `ActionKind::LessonModeToggle`

2. **`ChatClient`** (`src/ai/ChatClient.*`) — remote LLM call against an
   OpenAI-compatible `/chat/completions` endpoint. JSON is built with
   **nlohmann/json**; transport is delegated to an **`IHttpClient`** reference
   (default `CurlHttpClient`, injectable for tests).

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

Three-step pipeline:

```
piper_speak_and_play(text, model):
    1. piper_synthesize_wav(text, model, /tmp/hecquin_tts_*.wav)
           — shell: echo "text" | piper --model ... --output_file ...
           — sets DYLD_LIBRARY_PATH for macOS espeak-ng
    2. samples = wav_read_s16_mono(wav_file)
           — reads 44-byte WAV header, loads 16-bit PCM samples
    3. sdl_play_s16_mono_22k(samples)
           — opens SDL2 playback device at 22050 Hz if not already open
           — SDL callback feeds samples as playback progresses
           — polls until all samples consumed
    4. remove(wav_file)
```

---

## Action Types

| ActionKind | Produced by | `reply` content |
|---|---|---|
| `None` | Empty transcript | Empty |
| `LocalDevice` | Device regex match | "Okay, turn on/off the …" |
| `InteractionTopicSearch` | Story regex match | User prompt text |
| `InteractionMusicSearch` | Music regex match | User prompt text |
| `ExternalApi` | HTTP API call | API assistant reply |
| `AssistantSdk` | (reserved) | — |
| `GrammarCorrection` | `EnglishTutorProcessor` (Lesson mode) | "You said … / Better … / Reason …" |
| `LessonModeToggle` | Lesson start/end regex match | Short confirmation; flips `ListenerMode` |
| `PronunciationFeedback` | `PronunciationDrillProcessor` | Overall score + weakest word/phoneme + intonation note; next sentence announced after playback |
| `DrillModeToggle` | Drill start/end regex match | Short confirmation; flips `ListenerMode::Drill` |

---

## Threading

| Thread | Role |
|---|---|
| SDL audio callback | Appends mic samples to buffer under mutex |
| Main thread | Polls buffer, runs VAD, calls Whisper, drives TTS |
| `std::async` worker | Executes HTTP API call when no local match |
| SDL playback callback | Reads PCM samples from vector during playback |

All shared buffer access is guarded by `std::mutex` + `std::lock_guard`.

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
| `sound_libs.cmake` | Internal static libs: `hecquin_config`, `hecquin_ai`, `hecquin_voice_pipeline`, `hecquin_learning`, `hecquin_prosody`, `hecquin_pronunciation`, `hecquin_drill` |
| `voice_to_text.cmake` | `voice_detector` executable |
| `text_to_speech.cmake` | `text_to_speech` executable |
| `english_tutor.cmake` | `english_ingest` + `english_tutor` executables |
| `pronunciation_drill.cmake` | `pronunciation_drill` executable |
| `piper_speech.cmake` | `hecquin_piper_speech` static library |
| `sound_tests.cmake` | CTest unit tests (`openai_chat`, `local_intent`, `embedding_json`, `text_chunker`, `config_store`, `learning_store`, `phoneme_vocab`, `ctc_aligner`, `pronunciation_scorer`, `pitch_tracker`, `intonation_scorer`) |

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

### SQLite schema (learning DB, v2)

`LearningStore::kSchemaVersion == 2`. All migrations are idempotent
(`CREATE … IF NOT EXISTS`), stamped into `kv_metadata(schema_version)` on the
very first open and never downgraded afterwards. The table cluster is written
by different translation units under `src/learning/store/` — all sharing the
same class and DB file.

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

Indexes on `api_calls(ts)`, `api_calls(provider, ts)`, and `request_logs(ts)`
keep the dashboard's per-day aggregations cheap.

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
`english_ingest` binary then:

1. Lists every text file under `curriculum/` and `custom/`.
2. Computes an FNV-1a-based content fingerprint and skips already-ingested files
   (stored in `ingested_files`), unless `--rebuild` is passed.
3. Splits each file into ~1800-character chunks with a 200-character overlap,
   preferring whitespace breaks.
4. Embeds each chunk via `EmbeddingClient::embed`.
5. Upserts `documents` + `vec_documents` and records the file hash.

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
weakest word + phoneme, an intonation note, and a flag for the "next sentence"
announcement. After `VoiceListener` speaks the feedback, it invokes the queued
`drill_announce_cb_` to play the next target.

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
part of the shared v2 schema (see **English Tutor Subsystem → SQLite schema
(learning DB, v2)** for the exhaustive column list). Implementation lives in
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
