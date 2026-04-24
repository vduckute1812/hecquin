#!/usr/bin/env bash

jobs_count() {
  nproc 2>/dev/null || sysctl -n hw.ncpu
}

cmd_deps() {
  echo "Installing dependencies..."
  if [[ "$(uname)" == "Darwin" ]]; then
    brew install cmake git sdl2 espeak-ng sqlite
  else
    sudo apt update
    sudo apt install -y build-essential cmake pkg-config git libsdl2-dev libcurl4-openssl-dev \
      espeak-ng libespeak-ng-dev libsqlite3-dev
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

cmd_curriculum_fetch() {
  local force_flag="${1:-}"
  echo "📚 Fetching curriculum datasets..."
  bash "$ROOT_DIR/scripts/fetch_curriculum.sh" ${force_flag}
}

cmd_learning_ingest() {
  local ingest_bin="$PROJECT_BUILD_DIR/english_ingest"
  if [[ ! -x "$ingest_bin" ]]; then
    echo "Binary not found: $ingest_bin"
    echo "Run: ./dev.sh build"
    exit 1
  fi
  shift || true
  echo "🧠 Ingesting curriculum into vector DB..."
  (cd "$ROOT_DIR" && "$ingest_bin" "$@")
}

cmd_english_tutor() {
  local bin="$PROJECT_BUILD_DIR/english_tutor"
  if [[ ! -x "$bin" ]]; then
    echo "Binary not found: $bin"
    echo "Run: ./dev.sh build"
    exit 1
  fi
  (cd "$PROJECT_BUILD_DIR" && LD_LIBRARY_PATH="${WHISPER_INSTALL_DIR}/lib:${LD_LIBRARY_PATH:-}" ./english_tutor)
}

# Format every C/C++ source under sound/ using the in-repo .clang-format.
# Accepts an optional list of paths — handy for editor integrations.
cmd_fmt() {
  if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format not found; install it via 'brew install clang-format' or 'apt install clang-format'."
    exit 1
  fi
  shift || true
  local paths=("$@")
  if [[ ${#paths[@]} -eq 0 ]]; then
    mapfile -t paths < <(find "$ROOT_DIR/src" "$ROOT_DIR/tests" \
      -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.cc' -o -name '*.hh' \))
  fi
  echo "Formatting ${#paths[@]} file(s) with clang-format..."
  clang-format -i -style=file "${paths[@]}"
  echo "✅ clang-format complete."
}

# Install the pre-commit hook as a symlink into the repo's .git/hooks/ dir so
# edits to the tracked script are picked up without re-running the installer.
cmd_hooks_install() {
  local repo_root
  repo_root="$(git -C "$ROOT_DIR" rev-parse --show-toplevel 2>/dev/null || true)"
  if [[ -z "$repo_root" ]]; then
    echo "dev.sh hooks:install must be run inside a git checkout."
    exit 1
  fi
  local hook_src="$ROOT_DIR/scripts/pre-commit.sh"
  local hook_dir="$repo_root/.git/hooks"
  local hook_dst="$hook_dir/pre-commit"
  mkdir -p "$hook_dir"
  ln -sf "$hook_src" "$hook_dst"
  chmod +x "$hook_src"
  echo "✅ pre-commit hook installed: $hook_dst -> $hook_src"
}

