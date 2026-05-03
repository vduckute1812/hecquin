# `observability/`

In-process structured logger. Tiny header-only sink used everywhere the
rest of the codebase would otherwise reach for `std::cerr`.

## Files

| File | Purpose |
|---|---|
| `Log.hpp` | `hecquin::log::{debug,info,warn,error}(tag, msg)` + stream helpers. Reads `HECQUIN_LOG_LEVEL` (`debug` / `info` / `warn` / `error`; default `info`) and `HECQUIN_LOG_FORMAT` (`pretty` default, `json` for one-line JSON). |

## Output formats

- **pretty** — `[LEVEL] tag: message` (development).
- **json** — `{"ts":…,"level":…,"tag":…,"msg":…}` per line (CI / ingest pipelines).

Telemetry (`api_calls`, `pipeline_events`) does **not** flow through this
logger — those go straight into SQLite via `LearningStore` sinks when the
`LoggingHttpClient` / `PipelineEventSink` wiring is installed (see
[`../../README.md`](../../README.md) → Logging & telemetry). See
[`../../ARCHITECTURE.md#observability`](../../ARCHITECTURE.md#observability).

## Notes

- Header-only and lock-free on the hot path — the format check is a
  `thread_local` bool filled once.
- Do not add new sinks here without updating the dashboard's ingest
  expectations; prefer `PipelineEventSink` (in `voice/VoiceListener.hpp`)
  when the event is stage-level pipeline telemetry.
