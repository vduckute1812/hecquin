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
│   ├── WhisperEngine.hpp/cpp     — whisper.cpp inference wrapper
│   ├── VoiceListener.hpp/cpp     — VAD + pipeline orchestration
│   └── cli/VoiceDetector.cpp     — voice_detector executable entry point
├── ai/
│   ├── CommandProcessor.hpp/cpp  — local matching + HTTP routing
│   └── OpenAiChatContent.hpp/cpp — hand-rolled OpenAI JSON parser
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

Opens the default microphone via SDL2 at 16 kHz, mono, float32. An SDL audio callback appends samples to a mutex-protected ring buffer. The main thread calls `snapshotBuffer()` for a thread-safe copy. During TTS playback, `pauseDevice()` / `resumeDevice()` prevent mic echo. `limitBufferSize()` trims the buffer to keep only the last N seconds.

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

RAII wrapper around `whisper_context`. Loads a GGML model at construction. `transcribe()` runs greedy decoding on a float32 sample vector and returns joined segment text. Language is hard-coded to English.

### CommandProcessor

Two-stage routing:

1. **Local (synchronous)** — regex matching on normalized (lowercase, trimmed) transcript:
   - `turn on|turn off` + `air|switch` → `DeviceAction`
   - `tell me a story` → `TopicSearchAction`
   - `open music` → `MusicAction`

2. **External API (async)** — `std::async` HTTP POST to an OpenAI-compatible chat endpoint via libcurl. Only available when compiled with `HECQUIN_WITH_CURL` and a valid API key is configured. Timeout is 60 seconds.

`process()` blocks until a result is ready. `process_async()` returns `std::future<Action>`.

### OpenAiChatContent

Hand-rolled JSON extractor (no external library). Scans the response string for `"choices"` → `"message"` → `"content"`, then parses the JSON string value including escape sequences (`\n`, `\t`, `\"`, `\\`, `\uXXXX` → UTF-8). Returns `std::nullopt` on parse failure.

### ConfigStore / AppConfig / AiClientConfig

`ConfigStore` parses `.env/config.env` (key=value, `#` comments, `export` prefix, quoted values stripped). Process environment variables always take precedence over file entries.

`AiClientConfig` resolves settings with the following priority chains:

| Setting | Priority |
|---|---|
| API key | `OPENAI_API_KEY` → `HECQUIN_AI_API_KEY` → `GEMINI_API_KEY` → `GOOGLE_API_KEY` |
| Base URL | `OPENAI_BASE_URL` → `HECQUIN_AI_BASE_URL` (default: `https://api.openai.com/v1`) |
| Model | `HECQUIN_AI_MODEL` → `OPENAI_MODEL` (default: `gpt-4o-mini`) |

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

| Model | Default path |
|---|---|
| Whisper | `.env/shared/models/ggml-base.bin` |
| Piper voice | `.env/shared/models/piper/en_US-lessac-medium.onnx` |

---

## Development Workflow

`dev.sh` provides unified commands:

```
./dev.sh install:all       # full setup: deps → whisper → piper → models → cmake build
./dev.sh build             # cmake configure + build
./dev.sh run voice_detector
./dev.sh run text_to_speech "hello world"
./dev.sh speak "hello"     # shorthand for TTS playback
./dev.sh env:clean         # wipe platform build outputs
```
