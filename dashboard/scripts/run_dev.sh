#!/usr/bin/env bash
# Dev loop: live-reload, plain HTTP on :8000, no auth.

set -euo pipefail
cd "$(dirname "$0")/.."

: "${HECQUIN_HOST:=127.0.0.1}"
: "${HECQUIN_PORT:=8000}"
: "${HECQUIN_DB_PATH:=./tmp/dev.db}"

mkdir -p "$(dirname "${HECQUIN_DB_PATH}")"
export HECQUIN_HOST HECQUIN_PORT HECQUIN_DB_PATH
export HECQUIN_AUTH_TOKEN=""
export HECQUIN_SSL_CERT=""
export HECQUIN_SSL_KEY=""

PY="${PY:-.venv/bin/python3}"
[[ -x "${PY}" ]] || PY="$(command -v python3)"

exec "${PY}" -m uvicorn hecquin_dashboard.main:app \
    --host "${HECQUIN_HOST}" \
    --port "${HECQUIN_PORT}" \
    --reload \
    --reload-dir src
