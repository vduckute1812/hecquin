# Hecquin Sound Module

A cross-platform audio processing module for the Robot Tutor project, providing speech recognition (voice-to-text) and speech synthesis (text-to-speech) capabilities.

## Overview

The sound module consists of two main executables plus a small **AI / command routing** library used by the voice detector:


| Component          | Description                                                                                                                                                                                   | Technology                                                                             |
| ------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| **Voice Detector** | Captures audio, transcribes with Whisper, routes text through **CommandProcessor**, then speaks `**Action.reply`** with **Piper** + SDL playback (capture is paused during TTS to limit echo) | [whisper.cpp](https://github.com/ggerganov/whisper.cpp), **libcurl** (optional), Piper |
| **Text-to-Speech** | Synthesizes natural-sounding speech from text input                                                                                                                                           | [Piper TTS](https://github.com/rhasspy/piper)                                          |


Both audio programs use [SDL2](https://www.libsdl.org/) for cross-platform audio I/O. External AI calls use **OpenAI-compatible** `POST …/v1/chat/completions` when libcurl is available at configure time.

## Platform Support


| Platform                      | Environment | Status |
| ----------------------------- | ----------- | ------ |
| macOS (arm64/x86_64)          | Development | ✅      |
| Raspberry Pi (aarch64/armv7l) | Production  | ✅      |
| Linux (x86_64)                | Development | ✅      |


## Project Structure

```
sound/
├── CMakeLists.txt           # Main CMake configuration
├── dev.sh                   # Development automation script
├── src/                     # C++ sources (include root: `#include "voice/…"`, `ai/…`, `actions/…`, etc.)
│   ├── voice/               # Capture, Whisper, VAD listen loop, `main`
│   │   ├── VoiceDetector.cpp
│   │   ├── VoiceListener.cpp
│   │   ├── VoiceListener.hpp
│   │   ├── AudioCapture.cpp
│   │   ├── AudioCapture.hpp
│   │   ├── WhisperEngine.cpp
│   │   └── WhisperEngine.hpp
│   ├── config/              # Env / defaults (`ConfigStore`, `AppConfig`, …)
│   │   └── ai/              # `AiClientConfig` (API keys, model, base URL)
│   ├── actions/             # Routed intents → `Action` (`Action.hpp`, `*Action.hpp`, …)
│   ├── ai/                  # `CommandProcessor` + HTTP client + response parsing
│   │   ├── CommandProcessor.cpp
│   │   ├── CommandProcessor.hpp
│   │   ├── HttpClient.cpp
│   │   ├── HttpClient.hpp
│   │   ├── OpenAiChatContent.cpp
│   │   └── OpenAiChatContent.hpp
│   ├── tts/                 # Piper + SDL playback (static lib)
│   │   ├── PiperSpeech.cpp
│   │   └── PiperSpeech.hpp
│   └── cli/                 # Standalone TTS executable entry
│       └── TextToSpeech.cpp
├── tests/                   # Small unit tests (optional CMake target)
│   └── test_openai_chat_content.cpp
├── cmake/
│   ├── project_options.cmake      # Compiler flags (C++17)
│   ├── deps_whisper.cmake         # Whisper discovery
│   ├── deps_piper.cmake           # Piper TTS discovery
│   ├── deps_sdl2.cmake            # SDL2 discovery
│   ├── deps_curl.cmake            # libcurl (optional) for external AI
│   ├── dependency_libraries.cmake # Interface libraries
│   ├── targets.cmake              # Build targets
│   ├── voice_to_text.cmake        # Voice detector target
│   ├── text_to_speech.cmake       # TTS target
│   ├── sound_tests.cmake          # `hecquin_sound_test_openai_chat` + CTest
│   └── piper_speech.cmake         # Static lib hecquin_piper_speech
├── scripts/
│   ├── dev_project.sh          # Project build helpers
│   ├── dev_whisper.sh          # Whisper setup helpers
│   ├── dev_piper.sh            # Piper TTS setup helpers
│   └── install_build_all.sh    # One-shot: system deps, whisper, piper, models, CMake build
├── build/                   # Build output (per-platform)
│   ├── linux/
│   ├── mac/
│   └── rpi/
└── .env/                    # Local dependencies (not in git)
    ├── config.env           # Runtime config (API keys, audio device, etc.)
    ├── prompts/             # AI prompt files (editable without recompiling)
    │   └── system_prompt.txt
    ├── linux/               # Linux-specific installs
    ├── mac/                 # macOS binaries
    ├── rpi/                 # Raspberry Pi binaries
    └── shared/              # Shared resources
        ├── whisper.cpp/     # Whisper source
        ├── piper/           # Piper source
        └── models/          # AI models
            ├── ggml-base.bin         # Whisper model
            └── piper/
                └── en_US-lessac-medium.onnx  # Piper voice
```

## Quick Start

### Automated setup (all steps)

From the `sound` directory, this installs system packages (Homebrew or `apt` where used), clones and builds whisper.cpp, installs Piper, downloads the default Whisper model (`base`) and Piper voice (`en_US-lessac-medium`), then builds the CMake targets:

```bash
cd sound
./dev.sh install:all
# equivalent:
./scripts/install_build_all.sh
```

Optional environment variables (see `scripts/install_build_all.sh` for comments):


| Variable             | Purpose                                                                |
| -------------------- | ---------------------------------------------------------------------- |
| `SKIP_SYSTEM_DEPS=1` | Skip `./dev.sh deps` if packages are already installed                 |
| `WHISPER_MODEL`      | Whisper GGML model name (default: `base`)                              |
| `PIPER_VOICE`        | Piper voice id (default: `en_US-lessac-medium`)                        |
| `HECQUIN_ENV`        | Same as for `./dev.sh`: `dev` or `prod` to override platform detection |
| `HECQUIN_SOUND_BUILD_TESTS` | CMake: set `OFF` to skip the small `hecquin_sound_test_openai_chat` binary |


After this, continue with **Run** below.

### Unit tests (optional)

After a normal CMake configure/build from `sound/build`:

```bash
cd sound/build
ctest --output-on-failure
```

Or run the binary directly: `./hecquin_sound_test_openai_chat` (build tree location depends on your generator). For manual, step-by-step setup, use the following sections instead.

### 1. Install System Dependencies

**macOS:**

```bash
brew install cmake sdl2 espeak-ng
```

The Apple / Xcode SDK usually provides **libcurl** headers and libraries so CMake can enable HTTP AI; if configure warns that CURL was not found, install the Command Line Tools or a Homebrew `curl` and ensure CMake’s `FindCURL` can see it.

**Ubuntu/Debian:**

```bash
sudo apt install build-essential cmake pkg-config git libsdl2-dev libcurl4-openssl-dev \
  espeak-ng libespeak-ng-dev
```

**Raspberry Pi:**

```bash
sudo apt install build-essential cmake pkg-config git libsdl2-dev libcurl4-openssl-dev \
  espeak-ng libespeak-ng-dev
```

`libcurl4-openssl-dev` is required for **external AI** (OpenAI-compatible HTTP) in `voice_detector`. If CMake cannot find CURL, the binary still builds, but cloud routing returns a compile-time message instead of performing HTTP.

### 2. Setup Development Environment

```bash
cd sound

# Clone and build whisper.cpp
./dev.sh whisper:clone
./dev.sh whisper:build

# Download Whisper model (base model recommended)
./dev.sh whisper:download-model base

# Install Piper TTS
./dev.sh piper:install

# Download Piper voice model
./dev.sh piper:download-model en_US-lessac-medium
```

### 3. Build the Project

```bash
./dev.sh build
```

### 4. Run

**Voice-to-Text:**

```bash
./dev.sh run voice_detector
```

**Text-to-Speech:**

```bash
./dev.sh speak "Hello, I am your robot tutor!"
# or
./dev.sh run text_to_speech "Hello world"
```

## Detailed Usage

### Voice Detector (Speech Recognition + AI routing)

The voice detector listens on the default microphone, detects speech activity, transcribes audio with Whisper, then passes the joined transcript to **CommandProcessor**:

1. **Local commands** — matched with case-insensitive regular expressions (fast, no network).
2. **External API** — if nothing matches, sends the user text to an **OpenAI-compatible** chat completions endpoint (requires API key and a build with libcurl).

```bash
# Run with default model (ggml-base.bin)
./dev.sh run voice_detector
```

**Local routing (summary):**


| Spoken pattern (regex idea)                | `ActionKind`             | Typical `reply`                                                   |
| ------------------------------------------ | ------------------------ | ----------------------------------------------------------------- |
| `turn on` / `turn off` + `air` or `switch` | `LocalDevice`            | Short confirmation (e.g. turning air on/off)                      |
| `tell me a story`                          | `InteractionTopicSearch` | Prompt to choose a story / topic                                  |
| `open music`                               | `InteractionMusicSearch` | Prompt for music intent                                           |
| *(no match)*                               | `ExternalApi`            | Assistant text from the HTTP API (or an error / disabled message) |


`ActionKind::AssistantSdk` is reserved for a future subprocess (e.g. Python Google Assistant SDK); it is not wired yet.

**Audio configuration:**

- Sample rate: 16 kHz (required by Whisper)
- Channels: Mono
- Voice activity detection for automatic capture
- Language: English (configurable in code)
- Device selection via `AUDIO_DEVICE_INDEX` in `.env/config.env` (`-1` = system default; run once to see the numbered device list)

**External AI environment variables:**


| Variable                                   | Purpose                                         |
| ------------------------------------------ | ----------------------------------------------- |
| `OPENAI_API_KEY`, `HECQUIN_AI_API_KEY`, `GEMINI_API_KEY`, or `GOOGLE_API_KEY` | Bearer token for chat completions (first non-empty wins) |
| `OPENAI_BASE_URL` or `HECQUIN_AI_BASE_URL` | API root (default: `https://api.openai.com/v1`) |
| `OPENAI_MODEL` or `HECQUIN_AI_MODEL`       | Model name (default: `gpt-4o-mini`)             |
| `AUDIO_DEVICE_INDEX`                       | Capture device index (`-1` = system default)    |

**System prompt:** The AI system prompt is loaded from `.env/prompts/system_prompt.txt` at startup. Edit this file to change the assistant's personality or response style without recompiling. If the file is missing, a built-in default is used.


Use any provider that exposes the same JSON shape as OpenAI `/v1/chat/completions` (adjust base URL accordingly). The client does not call the native Gemini JSON API; it works with Google’s **OpenAI-compatible** Gemini host (see below).

#### Google Gemini 2.5 Flash-Lite (development)

[Gemini’s OpenAI compatibility layer](https://ai.google.dev/gemini-api/docs/openai) serves `POST …/chat/completions` at `https://generativelanguage.googleapis.com/v1beta/openai/`. Point the sound module at that root and set the model id to **`gemini-2.5-flash-lite`** (see [model card](https://ai.google.dev/gemini-api/docs/models/gemini-2.5-flash-lite)).

Example for `.env/config.env` or your shell (get a key from [Google AI Studio](https://aistudio.google.com/apikey)):

```bash
export GEMINI_API_KEY="your-api-key"
export HECQUIN_AI_BASE_URL="https://generativelanguage.googleapis.com/v1beta/openai"
export HECQUIN_AI_MODEL="gemini-2.5-flash-lite"
```

Then run `./dev.sh run voice_detector` as usual. The same variables work if you prefer `HECQUIN_AI_API_KEY` instead of `GEMINI_API_KEY`.

**Responses (speech):** After routing, the assistant **Action.reply** string is sent to **Piper** (same default `.onnx` as `text_to_speech`, set at CMake configure time). While Piper runs and audio plays, **microphone capture is paused** and the capture buffer is cleared so the assistant is less likely to be re-transcribed from the speakers.

**Example console output (reply is spoken, not printed as `💬`):**

```
Đang tải model Whisper...
Model loaded!
Tìm thấy 1 thiết bị ghi âm:
  [0] MacBook Pro Microphone
Audio device: 16000Hz, 1 channels, format=33056

🎤 Đang lắng nghe... (Nói bất kỳ lúc nào!)
⏹ Ghi âm hoàn thành!

🔍 Đang nhận diện...

📝 Kết quả:
  > Hello, how are you today?

🤖 Route: ExternalApi
🔊 Đang tổng hợp giọng nói...
📊 Loaded … samples (… s)
🔊 Đang phát giọng nói...
```

For a local phrase such as “turn on the air”, the route line shows `LocalDevice` and Piper speaks the short confirmation instead of calling the API. If Piper fails, an error is written to **stderr** and the reply text is included there for debugging.

### Transcribe Existing Audio

If you already have a WAV file, use Whisper directly:

```bash
./dev.sh transcribe audio.wav
```

This runs the locally built Whisper CLI with the default model.

### Text-to-Speech

Synthesize speech from text input using Piper TTS.

```bash
# Basic usage - plays audio immediately
./dev.sh speak "Hello world"

# Save to file instead of playing
./dev.sh run text_to_speech -o output.wav "Save this audio"

# Use custom voice model
./dev.sh run text_to_speech -m custom_voice.onnx "Hello world"
```

`./dev.sh speak` uses the built `text_to_speech` binary when available, and otherwise falls back to the installed Piper executable.

**Command line options:**


| Option                | Description                            |
| --------------------- | -------------------------------------- |
| `-m, --model <path>`  | Path to Piper voice model (.onnx)      |
| `-o, --output <path>` | Save audio to WAV file (skip playback) |
| `-h, --help`          | Show help message                      |


**Configuration:**

- Sample rate: 22050 Hz (Piper output)
- Channels: Mono
- Format: 16-bit PCM WAV

## Available Models

### Whisper Models

Download with: `./dev.sh whisper:download-model <model>`


| Model     | Size    | Speed    | Accuracy              |
| --------- | ------- | -------- | --------------------- |
| tiny      | ~39 MB  | Fastest  | Basic                 |
| tiny.en   | ~39 MB  | Fastest  | Basic (English only)  |
| **base**  | ~74 MB  | Fast     | Good                  |
| base.en   | ~74 MB  | Fast     | Good (English only)   |
| small     | ~244 MB | Moderate | Better                |
| small.en  | ~244 MB | Moderate | Better (English only) |
| medium    | ~769 MB | Slower   | Great                 |
| medium.en | ~769 MB | Slower   | Great (English only)  |
| large-v1  | ~1.5 GB | Slowest  | Best                  |
| large-v2  | ~1.5 GB | Slowest  | Best                  |
| large-v3  | ~1.5 GB | Slowest  | Best                  |


**Recommendation:** Use `base` or `base.en` for Raspberry Pi, `small` for development machines.

### Piper Voice Models

Download with: `./dev.sh piper:download-model <voice>`


| Voice                    | Language   | Quality          |
| ------------------------ | ---------- | ---------------- |
| **en_US-lessac-medium**  | US English | Medium (default) |
| en_US-amy-medium         | US English | Medium           |
| en_US-ryan-medium        | US English | Medium           |
| en_GB-alan-medium        | UK English | Medium           |
| en_GB-jenny_dioco-medium | UK English | Medium           |


## Development Commands

```bash
# Full install + build (deps, whisper, piper, models, CMake) — see Quick Start
./dev.sh install:all

# Show help and all available commands
./dev.sh

# Environment info
./dev.sh env:info

# Clean build artifacts
./dev.sh env:clean          # Clean current platform
./dev.sh env:clean mac      # Clean macOS build
./dev.sh env:clean rpi      # Clean Raspberry Pi build
```

## CMake Configuration

### Prerequisites

The CMake configuration expects:

1. SDL2 installed system-wide
2. Whisper built and installed in `.env/<platform>/whisper-install/`
3. Piper executable in `.env/<platform>/piper/`
4. Models in `.env/shared/models/`
5. **libcurl** (development package) for full external AI support — if `find_package(CURL)` fails, `voice_detector` still links a stub interface library and external HTTP is disabled at compile time (`HECQUIN_WITH_CURL` not defined)

### Build Manually

```bash
mkdir -p build/mac && cd build/mac
cmake ..
cmake --build . -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
```

Paths like `PIPER_EXECUTABLE` and the default Piper model are normally passed by `./dev.sh build`; when invoking CMake manually, set the same `-D` options as in `scripts/dev_project.sh` if needed.

### Custom Paths

Override default paths with CMake variables:

```bash
cmake .. \
  -DWHISPER_INSTALL_DIR=/path/to/whisper \
  -DDEFAULT_PIPER_MODEL_PATH=/path/to/voice.onnx
```

## Architecture

### Voice Detector Flow

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌──────────────────┐
│ Microphone  │───>│ SDL2 Audio  │───>│   Whisper   │───>│ CommandProcessor │
│   Input     │    │   Capture   │    │ Transcribe  │    │ (regex + HTTP)   │
└─────────────┘    └─────────────┘    └─────────────┘    └──────────────────┘
                         │                   │                      │
                         v                   v                      v
                   Float32 PCM          Transcript            Action.reply
                   16kHz Mono            (joined text)              │
                                                                    v
                        ┌──────────────────────────────────────────────┐
                        │ Pause capture → Piper (WAV) → SDL playback   │
                        │ 22050 Hz mono → speakers → resume capture    │
                        └──────────────────────────────────────────────┘
```

Transcription runs on the thread that owns the listen loop; routing to the external API is started via **std::async** so work can proceed on a worker thread while SDL continues capturing in its audio callback. **TTS** uses a **second** SDL audio device (playback); capture is paused for the Piper + playback phase so playback is not fed straight back into Whisper.

**C++ implementation notes:**

- **WhisperEngine** owns the `whisper_context` with **`std::unique_ptr`** and a custom deleter so the model is always freed on teardown. Known Whisper noise tokens (`[BLANK_AUDIO]`, `[NO_SPEECH]`, `[ Inaudible Remark ]`, etc.) are filtered out so they never reach the AI API.
- **CommandProcessor** delegates HTTP transport to **`HttpClient`** (`http_post_json`), which owns all libcurl boilerplate. JSON body assembly lives in `build_chat_body_()`, keeping `call_external_api_()` focused on orchestration and error handling.
- The VAD loop polls **`AudioCapture::snapshotBuffer()`** each interval; when end-of-speech is detected, **that iteration’s snapshot** is passed to Whisper (no extra full-buffer copy on every poll while recording).
- AI responses are sanitized for TTS (markdown stripped, whitespace normalized) before being sent to Piper.

### Text-to-Speech Flow

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│ Text Input  │───>│ Piper TTS   │───>│  SDL2 Audio │───> Speaker
│             │    │ (subprocess)│    │   Playback  │
└─────────────┘    └─────────────┘    └─────────────┘
                         │                   │
                         v                   v
                   WAV File             Int16 PCM
                   22050Hz              22050Hz Mono
```

## Troubleshooting

### Whisper library not found

```
whisper library not found. Run: ./dev.sh whisper:clone && ./dev.sh whisper:build
```

**Solution:** Build whisper.cpp:

```bash
./dev.sh whisper:clone
./dev.sh whisper:build
```

### Piper TTS not working

```
Lỗi chạy Piper TTS (exit code: 127)
```

**Solution:**

1. Install Piper: `./dev.sh piper:install`
2. On macOS, install espeak-ng: `brew install espeak-ng`
3. Download voice model: `./dev.sh piper:download-model en_US-lessac-medium`

### No audio input devices

```
Tìm thấy 0 thiết bị ghi âm
```

**Solution:**

- Check microphone permissions in System Settings
- Verify microphone is connected and working
- On macOS, grant Terminal/IDE microphone access

### Model not found

```
Model không tồn tại: .env/shared/models/ggml-base.bin
```

**Solution:** Download the model:

```bash
./dev.sh whisper:download-model base
./dev.sh piper:download-model en_US-lessac-medium
```

### External AI disabled or misconfigured

- **CMake:** `libcurl not found — voice_detector will build without HTTP AI` — install the CURL development package (`libcurl4-openssl-dev` on Debian/Ubuntu; Xcode Command Line Tools usually suffice on macOS), then re-run `./dev.sh build`.
- **Runtime:** `Set … for cloud replies` — export `OPENAI_API_KEY`, `HECQUIN_AI_API_KEY`, `GEMINI_API_KEY`, or `GOOGLE_API_KEY` before `./dev.sh run voice_detector` (for Gemini, set `HECQUIN_AI_BASE_URL` to Google’s OpenAI-compatible root; see **Google Gemini 2.5 Flash-Lite** above).
- **HTTP errors:** Check `OPENAI_BASE_URL` / model name; the response body (truncated) is included in the printed reply when the status code is not 2xx.

## License

Part of the Hecquin Robot Tutor project.

## Dependencies

- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) - MIT License
- [Piper TTS](https://github.com/rhasspy/piper) - MIT License
- [SDL2](https://www.libsdl.org/) - zlib License
- [libcurl](https://curl.se/libcurl/) (optional, for OpenAI-compatible chat API from `voice_detector`) — MIT-style license

