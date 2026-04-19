# Hecquin Sound Module — Architecture & Logic

## Overview

The Hecquin Sound Module is a cross-platform audio processing subsystem for the Robot Tutor project. It provides two core capabilities:

- **Voice-to-Text**: Speech recognition via whisper.cpp
- **Text-to-Speech**: Speech synthesis via Piper TTS

The system captures microphone audio, detects speech with VAD (Voice Activity Detection), transcribes it, routes the transcript to a local command matcher or external AI API, then speaks the reply back through the speaker.

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
│   ├── LocalIntentMatcher.hpp/cpp— regex-based local intents
│   ├── ChatClient.hpp/cpp        — remote LLM client (IHttpClient injected)
│   ├── CommandProcessor.hpp/cpp  — façade: matcher + chat
│   └── OpenAiChatContent.hpp/cpp — nlohmann/json response extractor
├── common/
│   └── StringUtils.hpp           — header-only trim/lower/starts_with
├── learning/
│   ├── LearningStore.hpp/cpp     — SQLite + sqlite-vec store
│   ├── EmbeddingClient.hpp/cpp   — Gemini embeddings (OpenAI-compat, batched)
│   ├── Ingestor.hpp/cpp          — curriculum → chunks → embeddings
│   ├── TextChunker.hpp/cpp       — standalone chunking utility
│   ├── RetrievalService.hpp/cpp  — vector search helpers
│   ├── ProgressTracker.hpp/cpp   — per-user learning log
│   ├── EnglishTutorProcessor.*   — RAG + grammar correction pipeline
│   └── cli/                      — `english_ingest`, `english_tutor` entries
├── actions/
│   ├── ActionKind.hpp            — enum: None / LocalDevice / TopicSearch / Music / ExternalApi / AssistantSdk
│   ├── Action.hpp                — {kind, reply, transcript}
│   ├── DeviceAction.hpp          — power control confirmation text
│   ├── ExternalApiAction.hpp     — wraps API response
│   ├── TopicSearchAction.hpp     — story/topic prompt
│   ├── MusicAction.hpp           — music intent
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
| `dependency_libraries.cmake` | Interface library wrappers |
| `voice_to_text.cmake` | `voice_detector` executable |
| `text_to_speech.cmake` | `text_to_speech` executable |
| `piper_speech.cmake` | `hecquin_piper_speech` static library |
| `sound_tests.cmake` | `hecquin_sound_test_openai_chat` test binary |

Key preprocessor defines set by CMake:

| Define | Meaning |
|---|---|
| `HECQUIN_WITH_CURL` | libcurl found; external API enabled |
| `DEFAULT_MODEL_PATH` | Whisper GGML model path |
| `DEFAULT_PIPER_MODEL_PATH` | Piper voice model path |
| `DEFAULT_CONFIG_PATH` | Absolute path to `.env/config.env` |
| `DEFAULT_PROMPTS_DIR` | Absolute path to `.env/prompts/` |
| `PIPER_EXECUTABLE` | Path to piper binary |

### Platform Detection

| Platform | Build prefix |
|---|---|
| macOS (Darwin) | `.env/mac/`, `build/mac/` |
| Raspberry Pi (aarch64/armv7l) | `.env/rpi/`, `build/rpi/` |
| Linux x86_64 | `.env/linux/`, `build/linux/` |

Whisper/Piper models are shared across platforms in `.env/shared/models/`.

---

## Default Model Paths

| Resource | Default path |
|---|---|
| Whisper model | `.env/shared/models/ggml-base.bin` |
| Piper voice model | `.env/shared/models/piper/en_US-lessac-medium.onnx` |
| Config | `.env/config.env` |
| System prompt | `.env/prompts/system_prompt.txt` |

---

## Development Workflow

`dev.sh` provides unified commands:

```
./dev.sh install:all       # full setup: deps → whisper → piper → models → cmake build
./dev.sh build             # cmake configure + build
./dev.sh run voice_detector
./dev.sh run text_to_speech "hello world"
./dev.sh speak "hello"     # shorthand for TTS playback
./dev.sh curriculum:fetch  # download public English-learning datasets
./dev.sh learning:ingest   # chunk → embed → insert into SQLite vector DB
./dev.sh english:tutor     # launch lesson-mode voice loop
./dev.sh env:clean         # wipe platform build outputs
```

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

### SQLite schema (learning DB)

| Table             | Notes                                                   |
|-------------------|---------------------------------------------------------|
| `documents`       | `id, source, kind, title, body, metadata_json, created_at` |
| `vec_documents`   | `USING vec0(embedding FLOAT[768])` (or BLOB fallback)   |
| `ingested_files`  | `path PRIMARY KEY, hash, ingested_at` — skip unchanged  |
| `sessions`        | `id, mode, started_at, ended_at`                        |
| `interactions`    | `id, session_id, user_text, corrected_text, grammar_notes, created_at` |
| `vocab_progress`  | `word PRIMARY KEY, first_seen_at, last_seen_at, seen_count, mastery` |

If `sqlite-vec` cannot be downloaded at configure time, `LearningStore` transparently
falls back to a BLOB-backed table and a C++ brute-force cosine scan — the rest of the
pipeline works unchanged (just slower on large curricula).

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
