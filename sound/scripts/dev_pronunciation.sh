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

# wav2vec2-phoneme CTC model (espeak IPA labels, trained on CommonVoice).
# We use the LV60 English-pretrained variant: the prior XLSR-53 mirror on
# `onnx-community/` started returning HTTP 401 (gated/removed) and LV60 is a
# better fit for an English pronunciation drill anyway. Same phoneme vocab
# shape, so `PhonemeVocab` loads either interchangeably.
#
# Override knobs (both default to the LV60 ONNX + upstream vocab combo):
#   HECQUIN_PRONUNCIATION_MODEL_URL  → any wav2vec2-phoneme ONNX model URL.
#     • Full precision (1.26 GB, default): …/onnx/model.onnx
#     • int8 quantized (318 MB):           …/onnx/model_quantized.onnx
#     • q4  quantized (242 MB):            …/onnx/model_q4.onnx
#     Use a quantized variant on Raspberry Pi / low-RAM devices.
#   HECQUIN_PRONUNCIATION_VOCAB_URL  → matching HuggingFace `vocab.json`.
PRONUNCIATION_MODEL_URL_DEFAULT="https://huggingface.co/onnx-community/wav2vec2-lv-60-espeak-cv-ft-ONNX/resolve/main/onnx/model.onnx"
PRONUNCIATION_VOCAB_URL_DEFAULT="https://huggingface.co/facebook/wav2vec2-lv-60-espeak-cv-ft/resolve/main/vocab.json"

ONNXRUNTIME_VERSION_DEFAULT="1.17.1"

_pronunciation_have() {
  command -v "$1" >/dev/null 2>&1
}

_pronunciation_fetch() {
  local url="$1"
  local out="$2"
  mkdir -p "$(dirname "$out")"
  if _pronunciation_have curl; then
    # --retry-all-errors covers transient 5xx / network blips.
    # -w/-sS shows the HTTP status on failure so the user can tell a gated
    # model (401/403) from a transient network error.
    local http_code
    http_code=$(curl -L --fail-with-body --progress-bar \
                     --retry 3 --retry-delay 2 --retry-all-errors \
                     --connect-timeout 15 \
                     -o "$out" -w "%{http_code}" "$url") || {
      echo "   curl failed for $url (http=${http_code:-n/a})" >&2
      return 1
    }
  elif _pronunciation_have wget; then
    wget -q --show-progress --tries=3 --timeout=30 -O "$out" "$url" || {
      echo "   wget failed for $url" >&2
      return 1
    }
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
      rm -f "$model_path"
      echo "⚠️  Failed to download phoneme model from:" >&2
      echo "      $model_url" >&2
      echo "   → If the URL returned 401/403 the model may be gated." >&2
      echo "   → On low-RAM hosts try the quantized variant:" >&2
      echo "        export HECQUIN_PRONUNCIATION_MODEL_URL=https://huggingface.co/onnx-community/wav2vec2-lv-60-espeak-cv-ft-ONNX/resolve/main/onnx/model_quantized.onnx" >&2
      echo "   → Otherwise drop the file yourself at: $model_path" >&2
    }
  else
    echo "✓ Phoneme model already present: $model_path"
  fi

  if [[ "$force" == "--force" || ! -s "$vocab_path" ]]; then
    echo "⬇️  Downloading phoneme vocab → $vocab_path"
    _pronunciation_fetch "$vocab_url" "$vocab_path" || {
      rm -f "$vocab_path"
      echo "⚠️  Failed to download phoneme vocab from:" >&2
      echo "      $vocab_url" >&2
      echo "   → Override with HECQUIN_PRONUNCIATION_VOCAB_URL or drop file at $vocab_path" >&2
    }
  else
    echo "✓ Phoneme vocab already present: $vocab_path"
  fi

  # onnxruntime: prefer system install, fall back to prebuilt tarball when the
  # system install genuinely isn't there.
  #
  # `brew --prefix onnxruntime` exits 0 even when the formula is *not*
  # installed — it just prints the path it would live at.  We therefore probe
  # the actual header + dylib on disk, not the brew metadata.
  local ort_root="$ENV_DIR/onnxruntime"
  if _onnxruntime_usable "$ort_root"; then
    echo "✓ onnxruntime already present: $(_onnxruntime_located "$ort_root")"
    return 0
  fi

  if [[ "$OS" == "Darwin" ]] && _pronunciation_have brew; then
    echo "Installing onnxruntime via Homebrew..."
    if brew install onnxruntime && _onnxruntime_usable "$ort_root"; then
      echo "✓ onnxruntime installed via Homebrew: $(_onnxruntime_located "$ort_root")"
      return 0
    fi
    echo "⚠️  Homebrew install of onnxruntime did not produce usable headers/dylib;" >&2
    echo "    falling back to GitHub prebuilt tarball." >&2
  fi

  if _install_onnxruntime_prebuilt "$ort_root" && _onnxruntime_usable "$ort_root"; then
    echo "✓ onnxruntime installed: $(_onnxruntime_located "$ort_root")"
    return 0
  fi

  echo "❌  onnxruntime is NOT available — pronunciation scoring will be stubbed." >&2
  echo "    Install manually, then re-run: cmake -S sound -B sound/build" >&2
  return 1
}

# Probe every location `deps_onnxruntime.cmake` searches, in the same order.
# Echoes the first hit's root dir (parent of include/lib), exits 0 on match.
_onnxruntime_located() {
  local proj_ort="$1"
  local candidates=(
    "${ONNXRUNTIME_ROOT:-}"
    "$proj_ort"
    "$ROOT_DIR/.env/mac/onnxruntime"
    "$ROOT_DIR/.env/rpi/onnxruntime"
    "$ROOT_DIR/.env/linux/onnxruntime"
    "$ROOT_DIR/.env/shared/onnxruntime"
    "/opt/homebrew/opt/onnxruntime"
    "/opt/homebrew"
    "/usr/local"
    "/usr"
  )
  local root
  for root in "${candidates[@]}"; do
    [[ -z "$root" ]] && continue
    if [[ -f "$root/include/onnxruntime_cxx_api.h" ]] || \
       [[ -f "$root/include/onnxruntime/onnxruntime_cxx_api.h" ]]; then
      echo "$root"
      return 0
    fi
  done
  return 1
}

_onnxruntime_usable() {
  _onnxruntime_located "$1" >/dev/null 2>&1
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
