#!/usr/bin/env bash

jobs_count() {
  nproc 2>/dev/null || sysctl -n hw.ncpu
}

run_piper() {
  local piper_bin="$1"
  shift

  if [[ "$OS" == "Darwin" ]]; then
    local fallback_libs=""
    local piper_lib_dir
    piper_lib_dir="$(dirname "$piper_bin")"
    [[ -d "$piper_lib_dir" ]] && fallback_libs="$piper_lib_dir"
    [[ -d "/opt/homebrew/opt/espeak-ng/lib" ]] && fallback_libs="${fallback_libs:+$fallback_libs:}/opt/homebrew/opt/espeak-ng/lib"
    [[ -d "/opt/homebrew/lib" ]] && fallback_libs="${fallback_libs:+$fallback_libs:}/opt/homebrew/lib"
    [[ -d "/usr/local/lib" ]] && fallback_libs="${fallback_libs:+$fallback_libs:}/usr/local/lib"

    if [[ -n "${DYLD_FALLBACK_LIBRARY_PATH:-}" ]]; then
      fallback_libs="${fallback_libs:+$fallback_libs:}${DYLD_FALLBACK_LIBRARY_PATH}"
    fi

    DYLD_FALLBACK_LIBRARY_PATH="$fallback_libs" "$piper_bin" "$@"
  else
    "$piper_bin" "$@"
  fi
}

binary_info() {
  local file_path="$1"
  file "$file_path" 2>/dev/null || true
}

binary_arch() {
  local info
  info="$(binary_info "$1")"

  if [[ "$info" == *"arm64"* ]]; then
    echo "arm64"
  elif [[ "$info" == *"x86_64"* ]]; then
    echo "x86_64"
  else
    echo "unknown"
  fi
}

install_piper_from_source_macos_arm64() {
  echo "Building native Piper from source for macOS arm64..."

  mkdir -p "$SHARED_ENV_DIR"

  if [[ ! -d "$PIPER_SRC_DIR/.git" ]]; then
    echo "Cloning Piper source..."
    git clone https://github.com/rhasspy/piper.git "$PIPER_SRC_DIR"
  else
    echo "Piper source already cloned: $PIPER_SRC_DIR"
    echo "Updating..."
    git -C "$PIPER_SRC_DIR" pull --ff-only
  fi

  rm -rf "$PIPER_BUILD_DIR"
  mkdir -p "$PIPER_BUILD_DIR"
  rm -rf "$PIPER_DIR"
  mkdir -p "$PIPER_DIR"

  cmake -S "$PIPER_SRC_DIR" -B "$PIPER_BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PIPER_DIR"

  cmake --build "$PIPER_BUILD_DIR" -j"$(jobs_count)"
  cmake --install "$PIPER_BUILD_DIR"

  if [[ -d "$PIPER_BUILD_DIR/pi/lib" ]]; then
    find "$PIPER_BUILD_DIR/pi/lib" -maxdepth 1 \( -name '*.dylib' -o -type l \) -exec cp -R {} "$PIPER_DIR/" \;
  fi

  if command -v install_name_tool >/dev/null 2>&1; then
    install_name_tool -add_rpath "@executable_path" "$PIPER_DIR/piper" 2>/dev/null || true
    [[ -f "$PIPER_DIR/piper_phonemize" ]] && install_name_tool -add_rpath "@executable_path" "$PIPER_DIR/piper_phonemize" 2>/dev/null || true
  fi

  chmod +x "$PIPER_DIR/piper"

  local installed_arch
  installed_arch="$(binary_arch "$PIPER_DIR/piper")"
  if [[ "$installed_arch" != "arm64" ]]; then
    echo "❌ Piper source build did not produce an arm64 binary (detected: $installed_arch)"
    exit 1
  fi
}

cmd_piper_install() {
  echo "Installing Piper TTS for $PLATFORM..."
  mkdir -p "$PIPER_DIR"

  if [[ "$OS" == "Darwin" && "$ARCH" == "arm64" ]]; then
    install_piper_from_source_macos_arm64

    echo ""
    echo "Verifying installation..."
    binary_info "$PIPER_DIR/piper"
    if ! run_piper "$PIPER_DIR/piper" --help >/dev/null 2>&1; then
      echo "❌ Native Piper install completed but runtime verification failed"
      echo "Ensure Homebrew dependencies exist: brew install cmake git sdl2 espeak-ng sqlite"
      exit 1
    fi

    echo ""
    echo "✅ Piper TTS installed to: $PIPER_DIR/piper"
    echo "Run './dev.sh piper:download-model' to download a voice model"
    return
  fi

  local piper_release
  case "$OS" in
    Darwin)
      if [[ "$ARCH" == "arm64" ]]; then
        piper_release="piper_macos_aarch64.tar.gz"
      else
        piper_release="piper_macos_x64.tar.gz"
      fi
      ;;
    Linux)
      if [[ "$ARCH" == "aarch64" ]]; then
        piper_release="piper_linux_aarch64.tar.gz"
      elif [[ "$ARCH" == "armv7l" ]]; then
        piper_release="piper_linux_armv7l.tar.gz"
      else
        piper_release="piper_linux_x86_64.tar.gz"
      fi
      ;;
    *)
      echo "Unsupported OS: $OS"
      exit 1
      ;;
  esac

  local piper_url="https://github.com/rhasspy/piper/releases/latest/download/$piper_release"
  echo "Downloading Piper ($piper_release) from: $piper_url"
  curl -L -o "/tmp/$piper_release" "$piper_url"

  echo "Extracting to $PIPER_DIR..."
  rm -rf "$PIPER_DIR"/*
  tar -xzf "/tmp/$piper_release" -C "$PIPER_DIR" --strip-components=1
  rm "/tmp/$piper_release"

  chmod +x "$PIPER_DIR/piper"

  echo ""
  echo "Verifying installation..."
  binary_info "$PIPER_DIR/piper"
  if ! run_piper "$PIPER_DIR/piper" --version >/dev/null 2>&1; then
    echo "⚠️  Piper launched with errors. On macOS, ensure espeak-ng is installed: brew install espeak-ng"
  fi

  echo ""
  echo "✅ Piper TTS installed to: $PIPER_DIR/piper"
  echo "Run './dev.sh piper:download-model' to download a voice model"
}

cmd_piper_download_model() {
  local voice="${1:-en_US-lessac-medium}"
  mkdir -p "$PIPER_MODELS_DIR"

  local lang_region="${voice%%-*}"
  local rest="${voice#*-}"
  local name="${rest%%-*}"
  local quality="${rest##*-}"
  local lang="${lang_region%%_*}"

  local voice_url="https://huggingface.co/rhasspy/piper-voices/resolve/main"
  local onnx_url="$voice_url/$lang/$lang_region/$name/$quality/${voice}.onnx"
  local json_url="$voice_url/$lang/$lang_region/$name/$quality/${voice}.onnx.json"

  echo "Downloading Piper voice model: $voice"
  echo "Model URL: $onnx_url"
  echo "Models are shared across platforms: $PIPER_MODELS_DIR"

  curl -L -o "$PIPER_MODELS_DIR/${voice}.onnx" "$onnx_url"
  curl -L -o "$PIPER_MODELS_DIR/${voice}.onnx.json" "$json_url"

  echo ""
  echo "✅ Voice model downloaded to: $PIPER_MODELS_DIR/${voice}.onnx"
}

cmd_speak() {
  local text="${2:-}"
  local voice="${3:-en_US-lessac-medium}"

  if [[ -z "$text" ]]; then
    echo "Usage: ./dev.sh speak \"text to speak\" [voice]"
    echo "Example: ./dev.sh speak \"Hello world\""
    exit 1
  fi

  if [[ -x "$PROJECT_BUILD_DIR/text_to_speech" ]]; then
    "$PROJECT_BUILD_DIR/text_to_speech" -m "$PIPER_MODELS_DIR/${voice}.onnx" "$text"
    return
  fi

  local piper_bin="$PIPER_DIR/piper"
  if [[ ! -x "$piper_bin" ]]; then
    piper_bin="$(which piper 2>/dev/null || true)"
  fi

  if [[ -z "$piper_bin" || ! -x "$piper_bin" ]]; then
    echo "Piper not found. Run: ./dev.sh piper:install"
    exit 1
  fi

  local model_path="$PIPER_MODELS_DIR/${voice}.onnx"
  if [[ ! -f "$model_path" ]]; then
    echo "Voice model not found. Run: ./dev.sh piper:download-model $voice"
    exit 1
  fi

  echo "$text" | run_piper "$piper_bin" --model "$model_path" --output-raw | \
    ffplay -f s16le -ar 22050 -ac 1 -nodisp -autoexit - 2>/dev/null || \
    (echo "$text" | run_piper "$piper_bin" --model "$model_path" --output_file /tmp/piper_out.wav && \
      afplay /tmp/piper_out.wav 2>/dev/null || aplay /tmp/piper_out.wav 2>/dev/null)
}

