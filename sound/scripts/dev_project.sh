#!/usr/bin/env bash

jobs_count() {
  nproc 2>/dev/null || sysctl -n hw.ncpu
}

cmd_deps() {
  echo "Installing dependencies..."
  if [[ "$(uname)" == "Darwin" ]]; then
    brew install cmake git sdl2 espeak-ng
  else
    sudo apt update
    sudo apt install -y build-essential cmake pkg-config git libsdl2-dev libcurl4-openssl-dev \
      espeak-ng libespeak-ng-dev
  fi
}

cmd_build() {
  mkdir -p "$PROJECT_BUILD_DIR"
  echo "Building project for $PLATFORM..."
  cmake -S "$ROOT_DIR" -B "$PROJECT_BUILD_DIR" \
    -DPIPER_EXECUTABLE="$PIPER_DIR/piper" \
    -DDEFAULT_PIPER_MODEL_PATH="$PIPER_MODELS_DIR/en_US-lessac-medium.onnx"
  cmake --build "$PROJECT_BUILD_DIR" -j"$(jobs_count)"
  echo "✅ Build complete: $PROJECT_BUILD_DIR"
}

cmd_run() {
  local target="${2:-}"

  if [[ -z "$target" ]]; then
    echo "Missing target name. Example: ./dev.sh run transcribe"
    exit 1
  fi
  if [[ ! -x "$PROJECT_BUILD_DIR/$target" ]]; then
    echo "Binary not found: $PROJECT_BUILD_DIR/$target"
    echo "Run: ./dev.sh build"
    exit 1
  fi

  shift 2 || true
  (cd "$PROJECT_BUILD_DIR" && LD_LIBRARY_PATH="${WHISPER_INSTALL_DIR}/lib:${LD_LIBRARY_PATH:-}" "./$target" "$@")
}

cmd_env_info() {
  echo "Environment Information:"
  echo "========================"
  echo "Platform:     $PLATFORM"
  echo "OS:           $OS"
  echo "Architecture: $ARCH"
  echo "Env Type:     $ENV_TYPE"
  echo ""
  echo "Directories:"
  echo "  Platform env:  $ENV_DIR"
  echo "  Shared env:    $SHARED_ENV_DIR"
  echo "  Build output:  $PROJECT_BUILD_DIR"
  echo "  Piper:         $PIPER_DIR"
  echo "  Models:        $MODELS_DIR"
  echo ""
  echo "Binaries:"
  if [[ -x "$PIPER_DIR/piper" ]]; then
    echo "  Piper: $(file "$PIPER_DIR/piper" | sed 's/.*: //')"
  else
    echo "  Piper: not installed"
  fi
}

cmd_env_clean() {
  local target="${1:-$PLATFORM}"
  echo "Cleaning build artifacts for: $target"
  rm -rf "$ROOT_DIR/build/$target"
  rm -rf "$ROOT_DIR/.env/$target"
  echo "✅ Cleaned: build/$target and .env/$target"
}

