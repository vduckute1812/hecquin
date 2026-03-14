#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

OPENCV_SRC_DIR="$ROOT_DIR/.env/opencv"
OPENCV_CONTRIB_DIR="$ROOT_DIR/.env/opencv_contrib"
OPENCV_BUILD_DIR="$ROOT_DIR/.env/opencv-build"
OPENCV_INSTALL_DIR="$ROOT_DIR/.env/opencv-install"

PROJECT_BUILD_DIR="$ROOT_DIR/build"

usage() {
  cat <<'EOF'
Usage:
  ./dev.sh deps
  ./dev.sh opencv:clone
  ./dev.sh opencv:build
  ./dev.sh build
  ./dev.sh run extract_buttons
  ./dev.sh run detect_buttons

Notes:
  - OpenCV is built/installed locally under .env/
  - Project is built under build/
EOF
}

cmd="${1:-}"
arg="${2:-}"

case "$cmd" in
  deps)
    sudo apt update
    sudo apt install -y build-essential cmake pkg-config git libgtk-3-dev
    ;;

  opencv:clone)
    mkdir -p "$ROOT_DIR/.env"
    if [[ ! -d "$OPENCV_SRC_DIR/.git" ]]; then
      git clone --branch 4.x https://github.com/opencv/opencv.git "$OPENCV_SRC_DIR"
    else
      echo "OpenCV already cloned: $OPENCV_SRC_DIR"
    fi

    if [[ ! -d "$OPENCV_CONTRIB_DIR/.git" ]]; then
      git clone --branch 4.x https://github.com/opencv/opencv_contrib.git "$OPENCV_CONTRIB_DIR"
    else
      echo "opencv_contrib already cloned: $OPENCV_CONTRIB_DIR"
    fi
    ;;

  opencv:build)
    if [[ ! -d "$OPENCV_SRC_DIR" ]]; then
      echo "Missing OpenCV source. Run: ./dev.sh opencv:clone"
      exit 1
    fi
    if [[ ! -d "$OPENCV_CONTRIB_DIR/modules" ]]; then
      echo "Missing opencv_contrib/modules. Run: ./dev.sh opencv:clone"
      exit 1
    fi

    mkdir -p "$OPENCV_BUILD_DIR"
    mkdir -p "$OPENCV_INSTALL_DIR"

    cmake -S "$OPENCV_SRC_DIR" -B "$OPENCV_BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$OPENCV_INSTALL_DIR" \
      -DOPENCV_EXTRA_MODULES_PATH="$OPENCV_CONTRIB_DIR/modules" \
      -DWITH_GTK=ON

    cmake --build "$OPENCV_BUILD_DIR" --target install -j"$(nproc)"
    ;;

  build)
    mkdir -p "$PROJECT_BUILD_DIR"
    cmake -S "$ROOT_DIR" -B "$PROJECT_BUILD_DIR"
    cmake --build "$PROJECT_BUILD_DIR" -j"$(nproc)"
    ;;

  run)
    if [[ -z "$arg" ]]; then
      echo "Missing target name. Example: ./dev.sh run extract_buttons"
      exit 1
    fi
    if [[ ! -x "$PROJECT_BUILD_DIR/$arg" ]]; then
      echo "Binary not found: $PROJECT_BUILD_DIR/$arg"
      echo "Run: ./dev.sh build"
      exit 1
    fi
    (cd "$PROJECT_BUILD_DIR" && "./$arg")
    ;;

  *)
    usage
    exit 1
    ;;
esac

