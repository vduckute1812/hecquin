#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Detect platform
OS="$(uname -s)"
ARCH="$(uname -m)"

# Determine environment: dev (Mac) or prod (Raspberry Pi)
if [[ "$OS" == "Darwin" ]]; then
  ENV_TYPE="dev"
  PLATFORM="mac"
elif [[ "$OS" == "Linux" && ("$ARCH" == "aarch64" || "$ARCH" == "armv7l") ]]; then
  ENV_TYPE="prod"
  PLATFORM="rpi"
else
  ENV_TYPE="dev"
  PLATFORM="linux"
fi

# Allow manual override via ENV variable
ENV_TYPE="${HECQUIN_ENV:-$ENV_TYPE}"

# Environment-specific directories
ENV_DIR="$ROOT_DIR/.env/$PLATFORM"
SHARED_ENV_DIR="$ROOT_DIR/.env/shared"

WHISPER_SRC_DIR="$SHARED_ENV_DIR/whisper.cpp"
WHISPER_BUILD_DIR="$ENV_DIR/whisper-build"
WHISPER_INSTALL_DIR="$ENV_DIR/whisper-install"
MODELS_DIR="$SHARED_ENV_DIR/models"  # Models are shared (platform-independent)

# Piper TTS directories (platform-specific binaries)
PIPER_DIR="$ENV_DIR/piper"
PIPER_MODELS_DIR="$MODELS_DIR/piper"  # Models are shared
PIPER_SRC_DIR="$SHARED_ENV_DIR/piper"
PIPER_BUILD_DIR="$ENV_DIR/piper-build"

PROJECT_BUILD_DIR="$ROOT_DIR/build/$PLATFORM"

echo "🖥️  Platform: $PLATFORM ($OS $ARCH)"
echo "📁 Environment: $ENV_TYPE"

usage() {
  cat <<'EOF'
Usage:
  ./dev.sh install:all           Full setup: deps, whisper, piper, models, build (scripts/install_build_all.sh)
  ./dev.sh deps
  ./dev.sh whisper:clone
  ./dev.sh whisper:build
  ./dev.sh whisper:download-model [model]
  ./dev.sh piper:install
  ./dev.sh piper:download-model [voice]
  ./dev.sh build
  ./dev.sh run <target> [args...]
  ./dev.sh transcribe <wav-file>        Transcribe a WAV via whisper.cpp
  ./dev.sh speak "text to speak"
  ./dev.sh curriculum:fetch [--force]   Download public English-learning datasets
  ./dev.sh learning:ingest [args...]    Embed curriculum/custom/ into the vector DB
  ./dev.sh english:tutor                Launch the English-tutor voice loop
  ./dev.sh env:info              Show current environment info
  ./dev.sh env:clean [platform]  Clean build artifacts

Environment:
  Set HECQUIN_ENV=dev|prod to override auto-detection
  Mac detected as 'dev', Raspberry Pi as 'prod'
  
  Structure:
    .env/mac/       - Mac-specific binaries (piper, whisper builds)
    .env/rpi/       - Raspberry Pi binaries
    .env/shared/    - Shared resources (source code, models)
    build/mac/      - Mac build output
    build/rpi/      - Raspberry Pi build output

Whisper models available:
  tiny, tiny.en, base, base.en, small, small.en,
  medium, medium.en, large-v1, large-v2, large-v3

Piper voices available:
  en_US-lessac-medium (default), en_US-amy-medium, en_US-ryan-medium,
  en_GB-alan-medium, en_GB-jenny_dioco-medium

Notes:
  - whisper.cpp is built/installed locally under .env/
  - Piper TTS is installed under .env/<platform>/piper/
  - Models are downloaded to .env/shared/models/ (shared across platforms)
  - Project is built under build/<platform>/
EOF
}

source "$ROOT_DIR/scripts/dev_project.sh"
source "$ROOT_DIR/scripts/dev_whisper.sh"
source "$ROOT_DIR/scripts/dev_piper.sh"

cmd="${1:-}"
arg="${2:-}"

case "$cmd" in
  install:all) bash "$ROOT_DIR/scripts/install_build_all.sh" ;;
  deps) cmd_deps ;;
  whisper:clone) cmd_whisper_clone ;;
  whisper:build) cmd_whisper_build ;;
  whisper:download-model) cmd_whisper_download_model "$arg" ;;
  piper:install) cmd_piper_install ;;
  piper:download-model) cmd_piper_download_model "$arg" ;;
  build) cmd_build ;;
  run) cmd_run "$@" ;;
  transcribe) cmd_transcribe "$@" ;;
  speak) cmd_speak "$@" ;;
  curriculum:fetch) cmd_curriculum_fetch "$arg" ;;
  learning:ingest) cmd_learning_ingest "$@" ;;
  english:tutor) cmd_english_tutor ;;
  env:info) cmd_env_info ;;
  env:clean) cmd_env_clean "$arg" ;;

  *)
    usage
    exit 1
    ;;
esac
