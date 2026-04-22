# Hecquin Dashboard

FastAPI + Jinja2 + Chart.js dashboard for the Hecquin Raspberry Pi stack.

Visualises:

- **Learning progress**: vocabulary growth, sessions per day, pronunciation
  score trend, weakest phonemes.
- **API telemetry**:
  - *Outbound* — every OpenAI-compatible / embedding POST issued by the C++
    runtime (logged via the `LoggingHttpClient` decorator into the shared
    SQLite `api_calls` table).
  - *Inbound* — every HTTP request served by this dashboard itself (logged
    via a Starlette middleware into `request_logs`).

All data lives in the same SQLite file written by
`sound/src/learning/LearningStore.cpp`; the dashboard only opens it
read-only except for the single `request_logs` writer.

---

## Architecture

```
Browser (https://<ddns>:8443)
      │   WAN 8443 ─── router forwards ──▶ Pi 443
      ▼
  uvicorn  (self-signed TLS)
      │
      ▼
  FastAPI ─── BearerAuthMiddleware ─── RequestLoggerMiddleware ─── routes
                                              │
                                              ▼
                                       request_logs (INSERT)
      │
      ▼
  Repositories (read-only SQLite)
      │
      ▼
        hecquin.db   ◀── api_calls / sessions / vocab_progress / ...
                                     (written by C++ LearningStore)
```

Design patterns in use:

| Layer | Pattern | File |
| --- | --- | --- |
| Outbound HTTP observability | **Decorator** over `IHttpClient` | `sound/src/ai/LoggingHttpClient.{hpp,cpp}` |
| Data access | **Repository** (one class per table) | `src/hecquin_dashboard/repositories/*.py` |
| Aggregation | **Service layer** with injected repos | `src/hecquin_dashboard/services/stats_service.py` |
| Chart payloads | **Strategy** (`LineChart`, `StackedBarChart`) | `src/hecquin_dashboard/services/chart_builder.py` |
| Dependency wiring | **FastAPI `Depends`** | `src/hecquin_dashboard/api/v1/deps.py` |
| Inbound telemetry | **ASGI middleware** | `src/hecquin_dashboard/middleware/request_logger.py` |
| Access control | **Bearer-token middleware** | `src/hecquin_dashboard/middleware/auth.py` |

---

## Repository layout

```
dashboard/
├── pyproject.toml
├── README.md
├── .env.example
├── certs/                         (self-signed cert lives here; gitignored)
├── scripts/
│   ├── gen_self_signed_cert.sh    openssl + SAN helper
│   ├── run_prod.sh                uvicorn + SSL + setcap sanity check
│   ├── run_dev.sh                 plain HTTP :8000 + reload
│   └── systemd/hecquin-dashboard.service
├── src/hecquin_dashboard/
│   ├── main.py                    app factory + entry point
│   ├── config.py                  pydantic-settings
│   ├── db.py                      read-only readers + single-writer
│   ├── middleware/                auth + request logging
│   ├── repositories/              one per table
│   ├── services/                  StatsService + ChartBuilder
│   ├── schemas/                   Pydantic DTOs
│   ├── api/v1/                    /api/v1/...
│   ├── views/pages.py             Jinja routes
│   ├── templates/
│   └── static/css + static/js
└── tests/                         seeded-SQLite fixtures + TestClient
```

---

## Quick start (local / dev)

```bash
cd dashboard
python3 -m venv .venv
.venv/bin/pip install -e '.[dev]'
cp .env.example .env                 # then edit HECQUIN_DB_PATH etc.
scripts/run_dev.sh                   # http://127.0.0.1:8000
```

Tests:

```bash
.venv/bin/pytest
```

The test suite materialises a temp SQLite using the same DDL as the C++
`LearningStore` v2, seeds deterministic rows, then exercises repositories,
services, and every HTTP route via `TestClient`.

---

## Production deploy on Raspberry Pi

Assumes the `sound/` module is already built and writing to
`~/hecquin/sound/.env/data/hecquin.db`.

```bash
# 1. Install
cd ~/hecquin/dashboard
python3 -m venv .venv
.venv/bin/pip install -e .
cp .env.example .env
```

Edit `.env`:

```
HECQUIN_DB_PATH=/home/pi/hecquin/sound/.env/data/hecquin.db
HECQUIN_HOST=0.0.0.0
HECQUIN_PORT=443
HECQUIN_SSL_CERT=certs/server.crt
HECQUIN_SSL_KEY=certs/server.key
HECQUIN_AUTH_TOKEN=<generate with: python3 -c "import secrets; print(secrets.token_urlsafe(32))">
```

```bash
# 2. TLS cert (self-signed).  SANs cover LAN IP + DDNS hostname.
scripts/gen_self_signed_cert.sh hecquin.duckdns.org "192.168.1.42"

# 3. Allow the python binary to bind privileged port 443 without root.
#    This is a one-time operation; repeat after every Python upgrade.
sudo setcap 'cap_net_bind_service=+ep' \
     "$(readlink -f .venv/bin/python3)"

# 4. Smoke test
scripts/run_prod.sh
# In another shell:
curl -k https://localhost/health
# → {"status":"ok","db":"..."}
```

### Router port forwarding

The home router only accepts port 8443 on the WAN side:

| WAN port | → LAN target         |
| -------- | -------------------- |
| 8443     | `<pi-ip>:443`        |

External URL therefore looks like `https://<ddns>:8443/`. The first hit
warns about the self-signed certificate; accept once per client.

### Systemd

```bash
sudo cp scripts/systemd/hecquin-dashboard.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now hecquin-dashboard
sudo systemctl status hecquin-dashboard
journalctl -u hecquin-dashboard -f
```

The unit sets `AmbientCapabilities=CAP_NET_BIND_SERVICE` so even if `setcap`
is ever reset, the service can still bind :443 under its `pi` user.

---

## C++ side

Two things were added to the C++ module:

1. **Schema v2 migration** in
   [`sound/src/learning/LearningStore.cpp`](../sound/src/learning/LearningStore.cpp):
   creates the `api_calls` and `request_logs` tables (idempotent; `IF NOT
   EXISTS`). `kSchemaVersion` bumped to 2 and re-stamped on every open.

2. **`LoggingHttpClient` decorator** in
   [`sound/src/ai/LoggingHttpClient.hpp`](../sound/src/ai/LoggingHttpClient.hpp) —
   wraps any `IHttpClient`, times each POST, and pushes an `ApiCallRecord`
   into a caller-supplied sink. The top-level CLIs
   (`EnglishTutorMain`, `EnglishIngest`) bind the sink to
   `LearningStore::record_api_call`, so every outbound LLM/embedding call
   is recorded without any change to `ChatClient` / `EmbeddingClient`.

---

## HTTP surface

| Route                                     | Purpose                                   |
| ----------------------------------------- | ----------------------------------------- |
| `GET /`                                   | Overview dashboard (Jinja)                |
| `GET /learning`                           | Learning charts (Jinja)                   |
| `GET /api-usage`                          | API telemetry charts + tables (Jinja)     |
| `GET /pronunciation`                      | Weakest phonemes (Jinja)                  |
| `GET /health`                             | Liveness (no auth)                        |
| `GET /api/docs`                           | Swagger UI                                |
| `GET /api/v1/overview`                    | KPI card data                             |
| `GET /api/v1/api-calls/daily?days=N`      | Outbound/inbound charts + top endpoints   |
| `GET /api/v1/api-calls/recent/outbound`   | Most recent outbound calls                |
| `GET /api/v1/api-calls/recent/inbound`    | Most recent inbound requests              |
| `GET /api/v1/learning/overview?days=N`    | Vocab + sessions + pronunciation charts   |
| `GET /api/v1/learning/vocab/top`          | Most-seen vocabulary                      |
| `GET /api/v1/learning/phonemes/weakest`   | Weakest phonemes                          |

With `HECQUIN_AUTH_TOKEN` set, every route except `/health` and
`/static/*` requires either
`Authorization: Bearer <token>` or a `hecquin_auth=<token>` cookie.

---

## Security notes

- **Self-signed cert** only protects the data in transit, not the endpoint.
  Always set `HECQUIN_AUTH_TOKEN` before exposing via the router.
- The dashboard opens the SQLite DB **read-only**, except for a single
  writer connection used exclusively by the request-log middleware (1 row
  per inbound request). C++ stays the authoritative writer for everything
  else — SQLite WAL mode lets both processes run concurrently without
  `database is locked` errors.
- `certs/` is git-ignored; do not commit keys.
- Consider rate-limiting (`slowapi`) if exposing to the internet at large.
