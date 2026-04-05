#!/usr/bin/env bash

jobs_count() {
  nproc 2>/dev/null || sysctl -n hw.ncpu
}

cmd_whisper_clone() {
  mkdir -p "$SHARED_ENV_DIR"
  if [[ ! -d "$WHISPER_SRC_DIR/.git" ]]; then
    echo "Cloning whisper.cpp..."
    git clone https://github.com/ggerganov/whisper.cpp.git "$WHISPER_SRC_DIR"
  else
    echo "whisper.cpp already cloned: $WHISPER_SRC_DIR"
    echo "Updating..."
    git -C "$WHISPER_SRC_DIR" pull --ff-only
  fi
}

cmd_whisper_build() {
  if [[ ! -d "$WHISPER_SRC_DIR" ]]; then
    echo "Missing whisper.cpp source. Run: ./dev.sh whisper:clone"
    exit 1
  fi

  mkdir -p "$WHISPER_BUILD_DIR" "$WHISPER_INSTALL_DIR"

  echo "Building whisper.cpp for $PLATFORM..."
  cmake -S "$WHISPER_SRC_DIR" -B "$WHISPER_BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$WHISPER_INSTALL_DIR" \
    -DWHISPER_BUILD_EXAMPLES=ON \
    -DWHISPER_SDL2=ON

  cmake --build "$WHISPER_BUILD_DIR" -j"$(jobs_count)"
  cmake --install "$WHISPER_BUILD_DIR"

  echo ""
  echo "whisper.cpp built and installed to: $WHISPER_INSTALL_DIR"
}

cmd_whisper_download_model() {
  local model="${1:-base}"
  mkdir -p "$MODELS_DIR"

  echo "Downloading model: ggml-$model.bin"

  if [[ -f "$WHISPER_SRC_DIR/models/download-ggml-model.sh" ]]; then
    (cd "$WHISPER_SRC_DIR/models" && bash download-ggml-model.sh "$model")
    if [[ -f "$WHISPER_SRC_DIR/models/ggml-$model.bin" ]]; then
      cp "$WHISPER_SRC_DIR/models/ggml-$model.bin" "$MODELS_DIR/"
      echo "Model copied to: $MODELS_DIR/ggml-$model.bin"
    fi
  else
    local model_url="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-$model.bin"
    curl -L -o "$MODELS_DIR/ggml-$model.bin" "$model_url"
    echo "Model downloaded to: $MODELS_DIR/ggml-$model.bin"
  fi
}

cmd_transcribe() {
  local audio_file="${2:-}"
  local model_file="${3:-$MODELS_DIR/ggml-base.bin}"

  if [[ -z "$audio_file" ]]; then
    echo "Usage: ./dev.sh transcribe <audio.wav> [model.bin]"
    echo "Example: ./dev.sh transcribe audio.wav"
    exit 1
  fi

  local whisper_main="$WHISPER_BUILD_DIR/bin/whisper-cli"
  if [[ ! -x "$whisper_main" ]]; then
    whisper_main="$WHISPER_BUILD_DIR/bin/main"
  fi
  if [[ ! -x "$whisper_main" ]]; then
    echo "whisper main not found. Run: ./dev.sh whisper:build"
    exit 1
  fi

  "$whisper_main" -m "$model_file" -f "$audio_file"
}

