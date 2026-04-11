#!/usr/bin/env bash
# Install system packages, fetch/build whisper.cpp and Piper, download default
# models, then build the sound module (voice_detector, text_to_speech).
#
# Usage (from anywhere):
#   ./sound/scripts/install_build_all.sh
# or:
#   cd sound && ./scripts/install_build_all.sh
#
# Environment:
#   SKIP_SYSTEM_DEPS=1   Skip ./dev.sh deps (brew/apt); use when deps are already installed
#   WHISPER_MODEL        Whisper GGML model name (default: base)
#   PIPER_VOICE          Piper voice id (default: en_US-lessac-medium)
#   HECQUIN_ENV          Passed through to dev.sh (dev|prod override)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

WHISPER_MODEL="${WHISPER_MODEL:-base}"
PIPER_VOICE="${PIPER_VOICE:-en_US-lessac-medium}"

echo "==> Hecquin sound: full install and build"
echo "    Whisper model: $WHISPER_MODEL"
echo "    Piper voice:   $PIPER_VOICE"
echo ""

if [[ "${SKIP_SYSTEM_DEPS:-}" == "1" ]]; then
  echo "==> Skipping system dependencies (SKIP_SYSTEM_DEPS=1)"
else
  echo "==> System dependencies (cmake, SDL2, espeak-ng, …)"
  ./dev.sh deps
fi

echo "==> whisper.cpp: clone"
./dev.sh whisper:clone

echo "==> whisper.cpp: build and install under .env/"
./dev.sh whisper:build

echo "==> Whisper model: $WHISPER_MODEL"
./dev.sh whisper:download-model "$WHISPER_MODEL"

echo "==> Piper TTS: install"
./dev.sh piper:install

echo "==> Piper voice: $PIPER_VOICE"
./dev.sh piper:download-model "$PIPER_VOICE"

echo "==> CMake project (voice_detector, text_to_speech)"
./dev.sh build

echo ""
echo "✅ Done. Binaries under build/<platform>/ — run ./dev.sh env:info or ./dev.sh run voice_detector"
