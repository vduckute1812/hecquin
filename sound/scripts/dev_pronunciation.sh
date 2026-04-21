#!/usr/bin/env bash
# Installer for the pronunciation-drill subsystem.
#
# Fetches:
#   * a wav2vec2-phoneme ONNX model + vocab.json under .env/shared/models/pronunciation/
#   * (best effort) an onnxruntime prebuilt into .env/<platform>/onnxruntime/ if
#     the system package manager does not already provide one.
#
# Idempotent: re-running only downloads what is missing. Pass --force to
# redownload the model.

PRONUNCIATION_MODELS_DIR="$MODELS_DIR/pronunciation"
PRONUNCIATION_MODEL_URL_DEFAULT="https://huggingface.co/onnx-community/wav2vec2-xlsr-53-espeak-cv-ft-ONNX/resolve/main/onnx/model.onnx"
PRONUNCIATION_VOCAB_URL_DEFAULT="https://huggingface.co/onnx-community/wav2vec2-xlsr-53-espeak-cv-ft-ONNX/resolve/main/vocab.json"

ONNXRUNTIME_VERSION_DEFAULT="1.17.1"

_pronunciation_have() {
  command -v "$1" >/dev/null 2>&1
}

_pronunciation_fetch() {
  local url="$1"
  local out="$2"
  mkdir -p "$(dirname "$out")"
  if _pronunciation_have curl; then
    curl -L --fail --progress-bar -o "$out" "$url"
  elif _pronunciation_have wget; then
    wget -q --show-progress -O "$out" "$url"
  else
    echo "Neither curl nor wget found; cannot download $url" >&2
    return 1
  fi
}

cmd_pronunciation_install() {
  local force="${1:-}"

  local model_url="${HECQUIN_PRONUNCIATION_MODEL_URL:-$PRONUNCIATION_MODEL_URL_DEFAULT}"
  local vocab_url="${HECQUIN_PRONUNCIATION_VOCAB_URL:-$PRONUNCIATION_VOCAB_URL_DEFAULT}"

  local model_path="$PRONUNCIATION_MODELS_DIR/wav2vec2_phoneme.onnx"
  local vocab_path="$PRONUNCIATION_MODELS_DIR/vocab.json"

  mkdir -p "$PRONUNCIATION_MODELS_DIR"

  if [[ "$force" == "--force" || ! -s "$model_path" ]]; then
    echo "⬇️  Downloading wav2vec2-phoneme ONNX model → $model_path"
    _pronunciation_fetch "$model_url" "$model_path" || {
      echo "⚠️  Failed to download phoneme model." >&2
      echo "   Set HECQUIN_PRONUNCIATION_MODEL_URL to override, or drop the file at $model_path." >&2
    }
  else
    echo "✓ Phoneme model already present: $model_path"
  fi

  if [[ "$force" == "--force" || ! -s "$vocab_path" ]]; then
    echo "⬇️  Downloading phoneme vocab → $vocab_path"
    _pronunciation_fetch "$vocab_url" "$vocab_path" || {
      echo "⚠️  Failed to download phoneme vocab." >&2
    }
  else
    echo "✓ Phoneme vocab already present: $vocab_path"
  fi

  # onnxruntime: prefer system install; only fetch prebuilt if truly missing.
  local ort_root="$ENV_DIR/onnxruntime"
  if [[ "$OS" == "Darwin" ]]; then
    if brew --prefix onnxruntime >/dev/null 2>&1; then
      echo "✓ onnxruntime already installed via Homebrew: $(brew --prefix onnxruntime)"
      return 0
    fi
    echo "Installing onnxruntime via Homebrew..."
    brew install onnxruntime || {
      echo "⚠️  brew install onnxruntime failed; falling back to prebuilt tarball." >&2
      _install_onnxruntime_prebuilt "$ort_root"
    }
    return 0
  fi

  if [[ -f /usr/include/onnxruntime_cxx_api.h ]] || \
     [[ -f /usr/local/include/onnxruntime_cxx_api.h ]]; then
    echo "✓ onnxruntime headers already present on the system."
    return 0
  fi

  _install_onnxruntime_prebuilt "$ort_root"
}

_install_onnxruntime_prebuilt() {
  local ort_root="$1"
  local version="${HECQUIN_ONNXRUNTIME_VERSION:-$ONNXRUNTIME_VERSION_DEFAULT}"
  local asset=""

  if [[ "$OS" == "Darwin" && "$ARCH" == "arm64" ]]; then
    asset="onnxruntime-osx-arm64-${version}.tgz"
  elif [[ "$OS" == "Darwin" ]]; then
    asset="onnxruntime-osx-x86_64-${version}.tgz"
  elif [[ "$ARCH" == "aarch64" || "$ARCH" == "armv7l" ]]; then
    asset="onnxruntime-linux-aarch64-${version}.tgz"
  else
    asset="onnxruntime-linux-x64-${version}.tgz"
  fi

  local url="https://github.com/microsoft/onnxruntime/releases/download/v${version}/${asset}"
  local tarball="$ENV_DIR/${asset}"

  mkdir -p "$ort_root"
  echo "⬇️  Downloading onnxruntime prebuilt: $url"
  if ! _pronunciation_fetch "$url" "$tarball"; then
    echo "⚠️  Could not download onnxruntime prebuilt. Install it manually and re-run cmake." >&2
    return 1
  fi

  tar -xzf "$tarball" -C "$ENV_DIR"
  rm -f "$tarball"
  # Flatten the versioned directory into ".env/<platform>/onnxruntime/".
  local extracted="$ENV_DIR/${asset%.tgz}"
  if [[ -d "$extracted" && ! -d "$ort_root/include" ]]; then
    rm -rf "$ort_root"
    mv "$extracted" "$ort_root"
  fi
  echo "✓ onnxruntime installed at $ort_root"
}

cmd_pronunciation_drill() {
  local bin="$PROJECT_BUILD_DIR/pronunciation_drill"
  if [[ ! -x "$bin" ]]; then
    echo "Binary not found: $bin"
    echo "Run: ./dev.sh build"
    exit 1
  fi
  (cd "$PROJECT_BUILD_DIR" && \
   LD_LIBRARY_PATH="${WHISPER_INSTALL_DIR}/lib:${LD_LIBRARY_PATH:-}" \
   ./pronunciation_drill "$@")
}
