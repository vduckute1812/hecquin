#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

WHISPER_SRC_DIR="$ROOT_DIR/.env/whisper.cpp"
WHISPER_BUILD_DIR="$ROOT_DIR/.env/whisper-build"
WHISPER_INSTALL_DIR="$ROOT_DIR/.env/whisper-install"
MODELS_DIR="$ROOT_DIR/.env/models"

PROJECT_BUILD_DIR="$ROOT_DIR/build"

usage() {
  cat <<'EOF'
Usage:
  ./dev.sh deps
  ./dev.sh whisper:clone
  ./dev.sh whisper:build
  ./dev.sh whisper:download-model [model]
  ./dev.sh build
  ./dev.sh run <target> [args...]

Models available:
  tiny, tiny.en, base, base.en, small, small.en,
  medium, medium.en, large-v1, large-v2, large-v3

Notes:
  - whisper.cpp is built/installed locally under .env/
  - Models are downloaded to .env/models/
  - Project is built under build/
EOF
}

cmd="${1:-}"
arg="${2:-}"

case "$cmd" in
  deps)
    echo "Installing dependencies..."
    if [[ "$(uname)" == "Darwin" ]]; then
      # macOS
      brew install cmake git sdl2
    else
      # Linux (Debian/Ubuntu)
      sudo apt update
      sudo apt install -y build-essential cmake pkg-config git libsdl2-dev
    fi
    ;;

  whisper:clone)
    mkdir -p "$ROOT_DIR/.env"
    if [[ ! -d "$WHISPER_SRC_DIR/.git" ]]; then
      echo "Cloning whisper.cpp..."
      git clone https://github.com/ggerganov/whisper.cpp.git "$WHISPER_SRC_DIR"
    else
      echo "whisper.cpp already cloned: $WHISPER_SRC_DIR"
      echo "Updating..."
      cd "$WHISPER_SRC_DIR" && git pull
    fi
    ;;

  whisper:build)
    if [[ ! -d "$WHISPER_SRC_DIR" ]]; then
      echo "Missing whisper.cpp source. Run: ./dev.sh whisper:clone"
      exit 1
    fi

    mkdir -p "$WHISPER_BUILD_DIR"
    mkdir -p "$WHISPER_INSTALL_DIR"

    echo "Building whisper.cpp..."
    cmake -S "$WHISPER_SRC_DIR" -B "$WHISPER_BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$WHISPER_INSTALL_DIR" \
      -DWHISPER_BUILD_EXAMPLES=ON \
      -DWHISPER_SDL2=ON

    cmake --build "$WHISPER_BUILD_DIR" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
    cmake --install "$WHISPER_BUILD_DIR"

    echo ""
    echo "whisper.cpp built and installed to: $WHISPER_INSTALL_DIR"
    ;;

  whisper:download-model)
    MODEL="${arg:-base}"
    mkdir -p "$MODELS_DIR"
    
    echo "Downloading model: ggml-$MODEL.bin"
    
    # Use the download script from whisper.cpp
    if [[ -f "$WHISPER_SRC_DIR/models/download-ggml-model.sh" ]]; then
      cd "$WHISPER_SRC_DIR/models"
      bash download-ggml-model.sh "$MODEL"
      # Copy or link to our models directory
      if [[ -f "$WHISPER_SRC_DIR/models/ggml-$MODEL.bin" ]]; then
        cp "$WHISPER_SRC_DIR/models/ggml-$MODEL.bin" "$MODELS_DIR/"
        echo "Model copied to: $MODELS_DIR/ggml-$MODEL.bin"
      fi
    else
      # Direct download from Hugging Face
      MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-$MODEL.bin"
      curl -L -o "$MODELS_DIR/ggml-$MODEL.bin" "$MODEL_URL"
      echo "Model downloaded to: $MODELS_DIR/ggml-$MODEL.bin"
    fi
    ;;

  build)
    mkdir -p "$PROJECT_BUILD_DIR"
    cmake -S "$ROOT_DIR" -B "$PROJECT_BUILD_DIR"
    cmake --build "$PROJECT_BUILD_DIR" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
    ;;

  run)
    if [[ -z "$arg" ]]; then
      echo "Missing target name. Example: ./dev.sh run transcribe"
      exit 1
    fi
    if [[ ! -x "$PROJECT_BUILD_DIR/$arg" ]]; then
      echo "Binary not found: $PROJECT_BUILD_DIR/$arg"
      echo "Run: ./dev.sh build"
      exit 1
    fi
    shift 2 || true
    # Forward remaining args (if any) to the executable
    (cd "$PROJECT_BUILD_DIR" && "./$arg" "$@")
    ;;

  transcribe)
    # Quick transcription using whisper.cpp main example
    AUDIO_FILE="${arg:-}"
    MODEL_FILE="${3:-$MODELS_DIR/ggml-base.bin}"
    
    if [[ -z "$AUDIO_FILE" ]]; then
      echo "Usage: ./dev.sh transcribe <audio.wav> [model.bin]"
      echo "Example: ./dev.sh transcribe audio.wav"
      exit 1
    fi
    
    WHISPER_MAIN="$WHISPER_BUILD_DIR/bin/whisper-cli"
    if [[ ! -x "$WHISPER_MAIN" ]]; then
      # Try alternative path
      WHISPER_MAIN="$WHISPER_BUILD_DIR/bin/main"
    fi
    if [[ ! -x "$WHISPER_MAIN" ]]; then
      echo "whisper main not found. Run: ./dev.sh whisper:build"
      exit 1
    fi
    
    "$WHISPER_MAIN" -m "$MODEL_FILE" -f "$AUDIO_FILE"
    ;;

  *)
    usage
    exit 1
    ;;
esac
