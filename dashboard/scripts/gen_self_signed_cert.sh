#!/usr/bin/env bash
# Generate a self-signed TLS certificate for the Hecquin dashboard.
#
# Usage:
#   scripts/gen_self_signed_cert.sh [CN] [extra_san,extra_san2,...]
#
# Examples:
#   scripts/gen_self_signed_cert.sh hecquin.local
#   scripts/gen_self_signed_cert.sh hecquin.duckdns.org "192.168.1.42,raspberrypi.local"
#
# Output files (0600) land under ./certs/:
#   - certs/server.crt   (x509 cert, 825-day validity — Apple's max)
#   - certs/server.key   (unencrypted RSA-4096 private key)
#
# Safe to re-run; it always overwrites.

set -euo pipefail

cd "$(dirname "$0")/.."

CN="${1:-hecquin.local}"
EXTRA_SAN="${2:-}"

mkdir -p certs
chmod 700 certs

SAN_ENTRIES=(
    "DNS:${CN}"
    "DNS:localhost"
    "IP:127.0.0.1"
    "IP:::1"
)

if [[ -n "${EXTRA_SAN}" ]]; then
    IFS=',' read -ra EXTRAS <<< "${EXTRA_SAN}"
    for e in "${EXTRAS[@]}"; do
        trimmed="$(echo "${e}" | xargs)"
        [[ -z "${trimmed}" ]] && continue
        if [[ "${trimmed}" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
            SAN_ENTRIES+=("IP:${trimmed}")
        else
            SAN_ENTRIES+=("DNS:${trimmed}")
        fi
    done
fi

SAN_STR="$(IFS=','; echo "${SAN_ENTRIES[*]}")"

echo ">>> Generating self-signed cert for CN=${CN}"
echo "    SAN: ${SAN_STR}"

openssl req -x509 \
    -newkey rsa:4096 -sha256 \
    -days 825 -nodes \
    -keyout certs/server.key \
    -out    certs/server.crt \
    -subj "/CN=${CN}" \
    -addext "subjectAltName=${SAN_STR}" \
    -addext "keyUsage=digitalSignature,keyEncipherment" \
    -addext "extendedKeyUsage=serverAuth"

chmod 600 certs/server.key
chmod 644 certs/server.crt

echo
echo "Done. Point HECQUIN_SSL_CERT / HECQUIN_SSL_KEY at:"
echo "    $(pwd)/certs/server.crt"
echo "    $(pwd)/certs/server.key"
