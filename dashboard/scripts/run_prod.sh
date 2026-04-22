#!/usr/bin/env bash
# Launch the dashboard on :443 with SSL, assuming a .venv + .env already exist.
#
# The Raspberry Pi router forwards WAN 8443 → Pi 443, so binding privileged
# port 443 is a one-time setup done via setcap on the python binary, avoiding
# `sudo` on the hot path.
#
# First-time setup (run once on the Pi):
#   python3 -m venv .venv
#   .venv/bin/pip install -e .
#   scripts/gen_self_signed_cert.sh <your-ddns-or-LAN-ip>
#   sudo setcap 'cap_net_bind_service=+ep' \
#        "$(readlink -f .venv/bin/python3)"
#
# Then normal runs just:
#   scripts/run_prod.sh

set -euo pipefail

cd "$(dirname "$0")/.."

if [[ -f .env ]]; then
    # Export variables so uvicorn (and pydantic-settings) see them.
    set -a
    # shellcheck disable=SC1091
    source .env
    set +a
fi

: "${HECQUIN_HOST:=0.0.0.0}"
: "${HECQUIN_PORT:=443}"
: "${HECQUIN_SSL_CERT:=certs/server.crt}"
: "${HECQUIN_SSL_KEY:=certs/server.key}"

PY="${PY:-.venv/bin/python3}"
if [[ ! -x "${PY}" ]]; then
    PY="$(command -v python3)"
fi

# Sanity: port 443 needs cap_net_bind_service or root.
if [[ "${HECQUIN_PORT}" -lt 1024 ]]; then
    REAL_PY="$(readlink -f "${PY}")"
    if ! getcap "${REAL_PY}" 2>/dev/null | grep -q cap_net_bind_service; then
        echo "!! Port ${HECQUIN_PORT} is privileged and '${REAL_PY}' lacks cap_net_bind_service."
        echo "   Grant with:  sudo setcap 'cap_net_bind_service=+ep' '${REAL_PY}'"
        echo "   (or set HECQUIN_PORT to >=1024 and let the router forward to it)"
    fi
fi

SSL_ARGS=()
if [[ -f "${HECQUIN_SSL_CERT}" && -f "${HECQUIN_SSL_KEY}" ]]; then
    SSL_ARGS+=(--ssl-certfile "${HECQUIN_SSL_CERT}" --ssl-keyfile "${HECQUIN_SSL_KEY}")
    echo ">>> HTTPS on https://${HECQUIN_HOST}:${HECQUIN_PORT}"
else
    echo "!! No TLS cert found at ${HECQUIN_SSL_CERT}; running plain HTTP (dev only)."
fi

exec "${PY}" -m uvicorn hecquin_dashboard.main:app \
    --host "${HECQUIN_HOST}" \
    --port "${HECQUIN_PORT}" \
    --proxy-headers \
    --forwarded-allow-ips="*" \
    --workers 2 \
    "${SSL_ARGS[@]}"
