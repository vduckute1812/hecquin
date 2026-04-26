# Hecquin Sound Module

A cross-platform audio processing module for the Robot Tutor project, providing speech recognition (voice-to-text) and speech synthesis (text-to-speech) capabilities.

> This README is the **user guide** — setup, commands, configuration keys, and troubleshooting.
> For the internal **source layout, component details, data-flow diagrams, SQLite schema,
> threading model, and CMake internals**, see [`ARCHITECTURE.md`](./ARCHITECTURE.md).
> For end-to-end **call-flow sequence diagrams** (boot, voice turn, TTS barge-in, music
> streaming + mid-song commands), see [`SEQUENCE_DIAGRAMS.md`](./SEQUENCE_DIAGRAMS.md).
> For the **voice-first UX layer** (earcons, wake-word / push-to-talk, sleep / wake intents,
> mode indicator, music confirm-cancel, per-user namespacing, welcome-back recap),
> see [`UX_FLOW.md`](./UX_FLOW.md).

## Overview

The sound module consists of several executables plus a small **AI / command routing** library used by the voice detector:


| Component                 | Description                                                                                                                                                                                   | Technology                                                                             |
| ------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| **Voice Detector**        | Captures audio, transcribes with Whisper, routes text through **CommandProcessor**, then speaks `**Action.reply`** with **Piper** + SDL playback (capture is paused during TTS to limit echo) | [whisper.cpp](https://github.com/ggerganov/whisper.cpp), **libcurl** (optional), Piper |
| **English Tutor**         | Grammar correction via Gemini chat completions + local SQLite/`sqlite-vec` RAG. Launched by voice (`"start english lesson"`) or via `./dev.sh english:tutor`.                                 | Whisper, Piper, libcurl, sqlite-vec                                                    |
| **Pronunciation Drill**   | Read-aloud drill that scores per-phoneme pronunciation (wav2vec2 + CTC forced alignment + GOP) and sentence intonation (YIN pitch tracker + DTW). Launched via `./dev.sh pronunciation:drill` or the voice phrase `"start pronunciation drill"`. | Whisper, Piper, onnxruntime (optional), espeak-ng                                      |
| **Text-to-Speech**        | Synthesizes natural-sounding speech from text input                                                                                                                                           | [Piper TTS](https://github.com/rhasspy/piper)                                          |


Both audio programs use [SDL2](https://www.libsdl.org/) for cross-platform audio I/O. External AI calls use **OpenAI-compatible** `POST …/v1/chat/completions` when libcurl is available at configure time.

## Platform Support


| Platform                      | Environment | Status |
| ----------------------------- | ----------- | ------ |
| macOS (arm64/x86_64)          | Development | ✅      |
| Raspberry Pi (aarch64/armv7l) | Production  | ✅      |
| Linux (x86_64)                | Development | ✅      |


## Project Layout (high-level)

```
sound/
├── CMakeLists.txt         # Main CMake configuration
├── dev.sh                 # Development automation script (main entry point)
├── src/                   # C++ sources — see ARCHITECTURE.md for the full tree
├── tests/                 # CTest unit tests
├── cmake/                 # Modular CMake files (one per dep / target)
├── scripts/               # Shell helpers called by dev.sh
├── build/{mac,rpi,linux}/ # Per-platform build output (git-ignored)
└── .env/                  # Local dependencies + secrets (git-ignored)
    ├── config.env         # Runtime config (API keys, audio device, …)
    ├── prompts/           # AI prompt files (editable without recompiling)
    ├── {mac,rpi,linux}/   # Per-platform installs (whisper, piper, onnxruntime)
    └── shared/            # Shared resources (whisper.cpp, piper, models, curricula)
```

For the full directory tree, component breakdown, and build-system internals see
[`ARCHITECTURE.md`](./ARCHITECTURE.md). For **per-folder** documentation — one
short README inside every sub-package under `src/` listing its files,
responsibilities, and the design patterns it uses — start at
[`src/README.md`](./src/README.md).

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
| `HECQUIN_SOUND_BUILD_TESTS` | CMake: set `OFF` to skip the `hecquin_sound_test_*` unit-test suite (defaults to `ON`). |


After this, continue with **Run** below.

### Unit tests (optional)

After a normal CMake configure/build from `sound/build`:

```bash
cd sound/build
ctest --output-on-failure
```

For what each test covers and how the suite is organised, see
[`ARCHITECTURE.md → Testing`](./ARCHITECTURE.md#testing).

For manual, step-by-step setup, use the following sections instead.

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
| `open music`                               | `MusicSearchPrompt`      | "What music would you like to play?" — enters `ListenerMode::Music` |
| *(next utterance in `Music` mode)*         | `MusicPlayback` / `MusicNotFound` | "Now playing …" on a hit (playback runs on a background thread so the mic stays live), "Sorry, I couldn't find that song." on a miss |
| `stop / cancel / exit / close / end music` | `MusicCancel`            | Aborts in-flight playback (or the pending search); exits `Music` mode |
| `pause music`                              | `MusicPause`             | Best-effort suspend (provider-dependent — works for the default SDL pipeline) |
| `continue / resume / unpause music`        | `MusicResume`            | Counterpart to pause                                              |
| *(no match)*                               | `ExternalApi`            | Assistant text from the HTTP API (or an error / disabled message) |


**Audio configuration:**

- Sample rate: 16 kHz (required by Whisper)
- Channels: Mono
- Voice activity detection for automatic capture (secondary gate logs an explicit `too_quiet` / `too_sparse` reason per rejection)
- Language: `HECQUIN_WHISPER_LANGUAGE` (default `en`, `""` = auto-detect)
- Device selection via `AUDIO_DEVICE_INDEX` in `.env/config.env` (`-1` = system default; run once to see the numbered device list)

**External AI environment variables:**


| Variable                                   | Purpose                                         |
| ------------------------------------------ | ----------------------------------------------- |
| `OPENAI_API_KEY`, `HECQUIN_AI_API_KEY`, `GEMINI_API_KEY`, or `GOOGLE_API_KEY` | Bearer token for chat completions (first non-empty wins) |
| `OPENAI_BASE_URL` or `HECQUIN_AI_BASE_URL` | API root (default: `https://api.openai.com/v1`) |
| `OPENAI_MODEL` or `HECQUIN_AI_MODEL`       | Model name (default: `gpt-4o-mini`)             |
| `AUDIO_DEVICE_INDEX`                       | Capture device index (`-1` = system default)    |
| `HECQUIN_WHISPER_LANGUAGE`                 | Whisper decode language (ISO-639-1, default `en`; `""` = auto-detect) |
| `HECQUIN_WHISPER_THREADS`                  | Whisper CPU threads (default: `max(2, hardware_concurrency()/2)`) |
| `HECQUIN_WHISPER_BEAM_SIZE`                | `0` = greedy (default), `>0` enables beam search |
| `HECQUIN_WHISPER_NO_SPEECH`                | Per-segment no-speech probability cutoff (default `0.6`) |
| `HECQUIN_WHISPER_MIN_ALNUM`                | Minimum alphanumeric chars to keep a decoded utterance (default `2`) |
| `HECQUIN_WHISPER_SUPPRESS_SEGS`            | `1` to silence per-segment `> …` stdout dumps |
| `HECQUIN_LOG_LEVEL`                        | `debug` / `info` / `warn` / `error` (default `info`) |
| `HECQUIN_LOG_FORMAT`                       | `pretty` (default) or `json` (one JSON object per log line) |

**Voice-first UX environment variables** (all optional; defaults preserve the legacy
behaviour — for the full design, diagrams, and rationale see [`UX_FLOW.md`](./UX_FLOW.md)):

| Variable                          | Default  | Purpose                                                                                  |
| --------------------------------- | -------- | ---------------------------------------------------------------------------------------- |
| `HECQUIN_EARCONS`                 | `1`      | `0` disables every earcon (start-listening / VAD-rejected / thinking / acknowledge / sleep / wake / network-offline) |
| `HECQUIN_EARCONS_DIR`             | unset    | Optional directory of `<name>.wav` overrides (mono int16, 22050 Hz)                      |
| `HECQUIN_WAKE_MODE`               | `always` | `always` \| `wake_word` \| `ptt`                                                         |
| `HECQUIN_WAKE_PHRASE`             | `hecquin\|hey hecquin\|hi hecquin\|hello hecquin` | Wake-phrase regex alternation (only used in `wake_word` mode)   |
| `HECQUIN_WAKE_WINDOW_MS`          | `8000`   | After a wake-phrase detection, follow-on transcripts route for this many ms              |
| `HECQUIN_DRILL_AUTO_ADVANCE`      | `1`      | `0` waits for an explicit `next` / `again` / `skip` (`DrillAdvance` intent) before announcing the next sentence |
| `HECQUIN_CONFIRM_CANCEL`          | `0`      | `1` enables the two-step `MusicCancel` confirmation (first cancel ducks + arms, second within window aborts) |
| `HECQUIN_CONFIRM_CANCEL_MS`       | `1200`   | Confirmation window length                                                               |
| `HECQUIN_DUCK_GAIN`               | `0.20`   | Music gain target while TTS speaks over a song (linear 0..1)                             |
| `HECQUIN_DUCK_RAMP_MS`            | `80`     | Linear ramp duration each side of the speak-begin / speak-end boundary                   |
| `HECQUIN_TTS_BARGE_IN`            | `0`      | `1` enables live-mic barge-in (raised VAD threshold instead of muting the mic during TTS)|

**System prompt:** The AI system prompt is loaded from `.env/prompts/system_prompt.txt` at startup. Edit this file to change the assistant's personality or response style without recompiling. If the file is missing, a built-in default is used.

**Music playback (YouTube Music):**

`open music` starts a two-turn flow: the assistant asks *"What music would you like to play?"* and the next utterance is forwarded to the configured `MusicProvider`. The default provider is **YouTube Music**, implemented by shelling out to `yt-dlp` and piping through `ffmpeg` to produce mono int16 PCM that feeds `StreamingSdlPlayer`. Apple Music and any other provider can plug in through the same `MusicProvider` interface in `src/music/`.

Prerequisites on the deploy target (mac dev, Raspberry Pi prod):

- `yt-dlp` (recommended: `pipx install yt-dlp` or `brew install yt-dlp` / `apt install yt-dlp` on newer distros).
- `ffmpeg` (`brew install ffmpeg` / `apt install ffmpeg`).
- Optional: a Netscape-format cookies file exported from a browser signed into your YouTube Premium Google account. Point `HECQUIN_YT_COOKIES_FILE` at it for ad-free, high-bitrate streams. Without it the provider still works but falls back to anonymous access.

| Variable                       | Purpose                                                              |
| ------------------------------ | -------------------------------------------------------------------- |
| `HECQUIN_MUSIC_PROVIDER`       | `youtube` (default). Unknown values fall back to YouTube.            |
| `HECQUIN_YT_COOKIES_FILE`      | Path to Netscape-format cookies.txt for Premium auth. Empty = anon.  |
| `HECQUIN_YT_DLP_BIN`           | Override the `yt-dlp` binary (default: look up on `$PATH`).          |
| `HECQUIN_FFMPEG_BIN`           | Override the `ffmpeg` binary (default: look up on `$PATH`).          |
| `HECQUIN_MUSIC_SAMPLE_RATE`    | PCM rate for SDL playback (default `44100`).                         |

Limitations (v1): `play()` is synchronous — the microphone stays muted for the full duration of the song, so "stop music" mid-playback is not yet supported over voice. Ctrl+C on the CLI, or the built-in signal handler, will still abort the `yt-dlp | ffmpeg` subprocess cleanly.


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

#### Secrets policy

- `.env/` is gitignored at the repo root — **never commit** API keys, even if the directory looks local.
- Treat `.env/config.env` as a secret file: if the key appears in a screenshot, chat log, issue, or terminal transcript, **rotate it immediately** in [Google AI Studio](https://aistudio.google.com/apikey) and delete the old one.
- Prefer the shell environment (`export GEMINI_API_KEY=…`) over the file when you are on a shared machine; `ConfigStore` reads env vars first and the file only as a fallback.
- For production / Raspberry Pi deployments, store the key in the OS keyring (`security add-generic-password` on macOS, `secret-tool store` on Linux) and export it at login, not in a checked-out file.

**Responses (speech):** After routing, the assistant **Action.reply** string is sent to **Piper** (same default `.onnx` as `text_to_speech`, set at CMake configure time). While Piper runs and audio plays, **microphone capture is paused** and the capture buffer is cleared so the assistant is less likely to be re-transcribed from the speakers.

**Example console output (reply is spoken, not printed as `💬`):**

```
Loading Whisper model...
Model loaded!
Found 1 recording device(s):
  [0] MacBook Pro Microphone
Audio device: 16000Hz, 1 channels, format=33056

🎤 Listening... (Speak anytime!)
⏹ Recording complete!

🔍 Recognizing...

📝 Result:
  > Hello, how are you today?

🤖 Route: ExternalApi
🔊 Synthesizing speech...
📊 Loaded … samples (… s)
🔊 Playing speech...
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

# Code quality (see sound/.clang-format and sound/.clang-tidy)
./dev.sh fmt                # Run clang-format on staged C/C++ files under sound/ (or: ./dev.sh fmt path/…)
./dev.sh hooks:install      # Symlink scripts/pre-commit.sh into .git/hooks/pre-commit
```

The pre-commit hook runs `clang-format -i` on staged `*.{c,cc,cpp,h,hpp}`
paths under `sound/` and re-stages them. A `.github/workflows/sound.yml`
workflow runs build + `ctest --output-on-failure` on `ubuntu-latest` and
`macos-latest`, plus a `clang-format --dry-run` check on changed files (a
companion `clang-tidy` job reports findings without failing the build for
now).

## English Tutor Mode

In addition to the default assistant, the sound module ships with an **English tutor**
that corrects grammar in what you say, backed by a local SQLite + `sqlite-vec` vector
database and Gemini embeddings for RAG. See
[`ARCHITECTURE.md → English Tutor Subsystem`](./ARCHITECTURE.md#english-tutor-subsystem)
for the runtime data flow, source layout, and SQLite schema.

### Workflow

```bash
# 1. Download public curriculum into .env/shared/learning/curriculum/
./dev.sh curriculum:fetch

# 2. (Optional) drop extra PDFs/markdown into .env/shared/learning/custom/
cp ~/Downloads/my_grammar_notes.md sound/.env/shared/learning/custom/

# 3. Build — this produces `english_ingest` and `english_tutor` alongside voice_detector
./dev.sh build

# 4. Embed everything into SQLite (uses GEMINI_API_KEY for embeddings)
./dev.sh learning:ingest
# Re-embed everything from scratch:
./dev.sh learning:ingest --rebuild

# 5. Launch the tutor
./dev.sh english:tutor
# Or from the default assistant, just say: "start english lesson"
# To leave: "exit lesson"
```

### Grammar correction reply shape

The tutor prompt (`.env/prompts/english_tutor_prompt.txt`) asks Gemini for exactly three short
lines, which `EnglishTutorProcessor::parse_tutor_reply` slices into a `GrammarCorrectionAction`:

```
You said: I goed to the store yesterday.
Better: I went to the store yesterday.
Reason: The past tense of "go" is the irregular form "went".
```

### Configuration knobs (`.env/config.env`)

| Key                              | Default                                              | Meaning                                  |
| -------------------------------- | ---------------------------------------------------- | ---------------------------------------- |
| `HECQUIN_AI_EMBEDDING_MODEL`     | `gemini-embedding-001`                               | Embedding model slug (OpenAI-compatible). `text-embedding-004` is no longer available on Gemini's OpenAI-compat endpoint. |
| `HECQUIN_AI_EMBEDDING_DIM`       | `768`                                                | Vector width (must match vec0 schema)    |
| `HECQUIN_LEARNING_DB_PATH`       | `.env/shared/learning/db/learning.sqlite`            | Location of the SQLite DB                |
| `HECQUIN_LEARNING_CURRICULUM_DIR`| `.env/shared/learning/curriculum`                    | Where `fetch_curriculum.sh` writes data  |
| `HECQUIN_LEARNING_CUSTOM_DIR`    | `.env/shared/learning/custom`                        | Drop custom text/md/pdf here             |
| `HECQUIN_LEARNING_RAG_TOPK`      | `3`                                                  | Chunks pulled into the tutor prompt      |
| `HECQUIN_LESSON_START_PHRASES`   | `start english lesson\|begin lesson\|start lesson`   | Voice triggers to enter lesson mode      |
| `HECQUIN_LESSON_END_PHRASES`     | `exit lesson\|end lesson\|stop lesson`               | Voice triggers to leave lesson mode      |
| `HECQUIN_DRILL_START_PHRASES`    | `start pronunciation drill\|begin pronunciation\|start drill` | Voice triggers to enter drill mode |
| `HECQUIN_DRILL_END_PHRASES`      | `exit drill\|end drill\|stop drill`                  | Voice triggers to leave drill mode       |
| `HECQUIN_DRILL_PASS_THRESHOLD`   | `75`                                                 | Overall score (0..100) that counts as a pass for the drill |
| `HECQUIN_PRONUNCIATION_MODEL`    | `.env/shared/models/pronunciation/wav2vec2_phoneme.onnx` | wav2vec2 phoneme-CTC ONNX model          |
| `HECQUIN_PRONUNCIATION_VOCAB`    | `.env/shared/models/pronunciation/vocab.json`        | HuggingFace-style token→id vocab for the above |
| `HECQUIN_PRONUNCIATION_CALIBRATION` | `.env/shared/models/pronunciation/calibration.json` | Optional per-IPA `{min_logp,max_logp}` overrides; missing → global anchors |
| `HECQUIN_ONNX_PROVIDER`          | `cpu`                                                | onnxruntime execution provider (`cpu` / `coreml` / `cuda`) |
| `HECQUIN_DRILL_SENTENCES`        | `.env/shared/learning/drill/sentences.txt`           | Optional newline-delimited pool of sentences the drill picks from |
| `HECQUIN_LOCALE`                 | `en-US`                                              | UI / prompt-lookup locale (currently English-only, plumbing only) |
| `HECQUIN_ESPEAK_VOICE`           | `en-us`                                              | espeak-ng voice used by G2P (`""` = library default) |

### Dependencies (extra for the tutor)

- **SQLite3** development package (`brew install sqlite` on macOS, `apt install libsqlite3-dev` on Debian/Ubuntu).
- **sqlite-vec** — downloaded automatically as an amalgamation into `third_party/sqlite-vec/` on first `cmake` configure. Override with `-DHECQUIN_DISABLE_SQLITE_VEC=ON` to fall back to in-process brute-force cosine search over a BLOB column.
- **libcurl** (same as the base bot) — required for the embeddings HTTP call.


## Logging & telemetry

### Structured logs

The module logs through a small in-process logger at
`src/observability/Log.hpp`:

- `HECQUIN_LOG_LEVEL=debug|info|warn|error` (default `info`) controls verbosity.
- `HECQUIN_LOG_FORMAT=json` switches from the default pretty one-liner to
  JSON Lines (`{"ts":…, "level":…, "tag":…, "msg":…}`), convenient for log
  scrapers and the dashboard ingest pipeline. Any other value (or unset)
  keeps the pretty format.

### API call telemetry

Every outbound chat/embedding call is logged automatically: the
`RetryingHttpClient` → `LoggingHttpClient` → `CurlHttpClient` decorator chain
wraps every HTTP call and writes one row per request into the `api_calls`
SQLite table (latency, status, request/response bytes, error — with retries
collapsed to the terminal outcome). No extra flag is required — when libcurl
is available, telemetry is on.

### Pipeline events (v3 schema)

Stage-level latencies and outcomes (`vad_gate`, `whisper`, `piper_synth`,
`drill_align`, `drill_pick`, …) are written to the `pipeline_events` table
via the same sink mechanism. The sibling **`dashboard/`** module consumes
both tables (plus its own `request_logs`) to render daily traffic, latency,
gate-failure, and error-rate charts over HTTPS; see
[`../dashboard/README.md`](../dashboard/README.md) for setup, and
[`ARCHITECTURE.md → Observability`](./ARCHITECTURE.md#observability) for the
internal flow.

## Pronunciation & Intonation Drill

The drill extends the listener with a third mode (`ListenerMode::Drill`) that
scores *read-aloud* attempts against a known reference sentence — so we can
provide concrete, local, per-phoneme feedback without any cloud call on the
acoustic path. See
[`ARCHITECTURE.md → Pronunciation & Intonation Drill Subsystem`](./ARCHITECTURE.md#pronunciation--intonation-drill-subsystem)
for the full scoring pipeline.

### Install the extra dependencies

```bash
# Download the wav2vec2 phoneme-CTC ONNX model + vocab.
./dev.sh pronunciation:install

# onnxruntime:
brew install onnxruntime              # macOS (Homebrew)
sudo apt install libonnxruntime-dev   # Debian / Ubuntu
# Raspberry Pi / offline machines — `pronunciation:install` will drop
# a prebuilt tarball into `.env/<platform>/onnxruntime/` for you.

# espeak-ng is reused from Piper and must already be installed.
```

The build gracefully degrades when onnxruntime is not present: the module
compiles, but `PhonemeModel::load()` returns false and the drill reports
"pronunciation scoring unavailable — install onnxruntime".

### Running

```bash
# Standalone binary (always starts directly in drill mode).
./dev.sh pronunciation:drill

# Or from the voice detector, just say the trigger phrase:
#   "start pronunciation drill"   → enter drill mode
#   "exit drill"                  → return to assistant mode
```

The robot picks sentences from `HECQUIN_DRILL_SENTENCES` (falling back to a
built-in English pool) using a **spaced-repetition picker**: each draw is
biased toward sentences whose IPA plan covers the learner's current weakest
phonemes (from `phoneme_mastery`), with a small epsilon chance of a plain
rotation so the learner still sees variety. The reference PCM + pitch contour
for each sentence is memoised in a small LRU (keyed by sentence hash) so
repeats and rotate-backs replay cached audio without re-invoking Piper. Every
attempt is stored in `pronunciation_attempts` and aggregated per-IPA in
`phoneme_mastery` — see the v3 schema table in
[`ARCHITECTURE.md`](./ARCHITECTURE.md#sqlite-schema-learning-db-v3).

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
Piper TTS failed (exit code: 127)
```

**Solution:**

1. Install Piper: `./dev.sh piper:install`
2. On macOS, install espeak-ng: `brew install espeak-ng`
3. Download voice model: `./dev.sh piper:download-model en_US-lessac-medium`

### No audio input devices

```
Found 0 recording device(s)
```

**Solution:**

- Check microphone permissions in System Settings
- Verify microphone is connected and working
- On macOS, grant Terminal/IDE microphone access

### Model not found

```
Model not found: .env/shared/models/ggml-base.bin
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

Released under the [GNU General Public License v3.0](../LICENSE). Part of the
Hecquin Robot Tutor project. See `LICENSE` at the repo root for the full
text. Anyone who distributes a modified version of this code (in source or
binary form) must release their changes under the same license.

## Dependencies

- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) - MIT License
- [Piper TTS](https://github.com/rhasspy/piper) - MIT License
- [SDL2](https://www.libsdl.org/) - zlib License
- [libcurl](https://curl.se/libcurl/) (optional, for OpenAI-compatible chat API from `voice_detector`) — MIT-style license

