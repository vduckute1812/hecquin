# Hecquin Sound Module

A cross-platform audio processing module for the Robot Tutor project, providing speech recognition (voice-to-text) and speech synthesis (text-to-speech) capabilities.

## Overview

The sound module consists of two main components:


| Component          | Description                                                   | Technology                                              |
| ------------------ | ------------------------------------------------------------- | ------------------------------------------------------- |
| **Voice Detector** | Captures audio from microphone and transcribes speech to text | [whisper.cpp](https://github.com/ggerganov/whisper.cpp) |
| **Text-to-Speech** | Synthesizes natural-sounding speech from text input           | [Piper TTS](https://github.com/rhasspy/piper)           |


Both components use [SDL2](https://www.libsdl.org/) for cross-platform audio I/O.

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
├── voice_detector.cpp       # Voice-to-text implementation
├── text_to_speech.cpp       # Text-to-speech implementation
├── cmake/
│   ├── project_options.cmake      # Compiler flags (C++17)
│   ├── deps_whisper.cmake         # Whisper discovery
│   ├── deps_piper.cmake           # Piper TTS discovery
│   ├── deps_sdl2.cmake            # SDL2 discovery
│   ├── dependency_libraries.cmake # Interface libraries
│   ├── targets.cmake              # Build targets
│   ├── voice_to_text.cmake        # Voice detector target
│   └── text_to_speech.cmake       # TTS target
├── scripts/
│   ├── dev_project.sh          # Project build helpers
│   ├── dev_whisper.sh          # Whisper setup helpers
│   ├── dev_piper.sh            # Piper TTS setup helpers
│   └── install_build_all.sh    # One-shot: system deps, whisper, piper, models, CMake build
├── build/                   # Build output (per-platform)
│   ├── mac/
│   └── rpi/
└── .env/                    # Local dependencies (not in git)
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

| Variable | Purpose |
| -------- | ------- |
| `SKIP_SYSTEM_DEPS=1` | Skip `./dev.sh deps` if packages are already installed |
| `WHISPER_MODEL` | Whisper GGML model name (default: `base`) |
| `PIPER_VOICE` | Piper voice id (default: `en_US-lessac-medium`) |
| `HECQUIN_ENV` | Same as for `./dev.sh`: `dev` or `prod` to override platform detection |

After this, continue with **Run** below. For manual, step-by-step setup, use the following sections instead.

### 1. Install System Dependencies

**macOS:**

```bash
brew install cmake sdl2 espeak-ng
```

**Ubuntu/Debian:**

```bash
sudo apt install build-essential cmake pkg-config git libsdl2-dev espeak-ng libespeak-ng-dev
```

**Raspberry Pi:**

```bash
sudo apt install build-essential cmake pkg-config git libsdl2-dev espeak-ng libespeak-ng-dev
```

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

### Voice Detector (Speech Recognition)

The voice detector listens on the default microphone, detects speech activity, and transcribes captured audio to text using Whisper.

```bash
# Run with default model (ggml-base.bin)
./dev.sh run voice_detector
```

**Configuration:**

- Sample rate: 16 kHz (required by Whisper)
- Channels: Mono
- Voice activity detection for automatic capture
- Language: English (configurable in code)

**Example output:**

```
Đang tải model Whisper...
Model loaded!
Tìm thấy 1 thiết bị ghi âm:
  [0] MacBook Pro Microphone
Audio device: 16000Hz, 1 channels, format=33056

🎤 Đang lắng nghe giọng nói...
⏹ Ghi âm hoàn thành!

🔍 Đang nhận diện giọng nói...

📝 Kết quả:
  > Hello, how are you today?
```

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

### Build Manually

```bash
mkdir -p build/mac && cd build/mac
cmake ../..
make -j$(nproc)
```

### Custom Paths

Override default paths with CMake variables:

```bash
cmake ../.. \
  -DWHISPER_INSTALL_DIR=/path/to/whisper \
  -DDEFAULT_PIPER_MODEL_PATH=/path/to/voice.onnx
```

## Architecture

### Voice Detector Flow

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│ Microphone  │───>│ SDL2 Audio  │───>│   Whisper   │
│   Input     │    │   Capture   │    │ Transcribe  │
└─────────────┘    └─────────────┘    └─────────────┘
                         │                   │
                         v                   v
                   Float32 PCM          Text Output
                   16kHz Mono
```

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

## License

Part of the Hecquin Robot Tutor project.

## Dependencies

- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) - MIT License
- [Piper TTS](https://github.com/rhasspy/piper) - MIT License
- [SDL2](https://www.libsdl.org/) - zlib License

