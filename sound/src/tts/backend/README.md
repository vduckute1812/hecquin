# `tts/backend/`

Strategy interface + implementations for **"synthesise text → int16 PCM"**.
The Piper facade holds an `IPiperBackend` and never knows which concrete
strategy answered; that makes fallback policy and testing trivial.

## Files

| File | Purpose |
|---|---|
| `IPiperBackend.hpp` | Strategy interface: `synthesize(text, model_path, samples_out, sample_rate_out) → bool`. Implementations must leave the outputs untouched on failure. |
| `PiperSpawn.hpp/cpp` | Template Method: the fixed `posix_spawn → write text → read PCM → collect stderr` skeleton shared by the concrete backends. Returns a `PiperSpawnResult` so error paths stay uniform. |
| `PiperPipeBackend.hpp/cpp` | Primary. Spawns `piper --stdin --output_raw --json_input` and streams raw PCM back over a pipe. No disk I/O. |
| `PiperShellBackend.hpp/cpp` | Legacy fallback. `echo "text" | piper --output_file /tmp/…wav` + `WavReader::read_pcm_s16_mono`. Kept for environments where stdin mode is unavailable. |
| `PiperFallbackBackend.hpp/cpp` | Composite strategy: try the primary, fall back on failure. Clears `samples_out` / `sample_rate_out` between attempts so the facade never sees partial state. `make_default_backend()` wires the default `Pipe → Shell` chain. |

## Tests

- `tests/test_piper_backend_fallback.cpp` — Stubbed `IPiperBackend` pairs driving the composite: primary-wins, fallback-wins, both-fail, null-primary safety.

## Notes

- Keep the Strategy interface narrow. If a backend needs more parameters,
  extend the struct it returns — do **not** add overloads to
  `IPiperBackend::synthesize`.
- `PiperSpawn` uses `posix_spawn` directly (not `popen`) so we can survive
  `SIGCHLD` and the EDR agents on macOS. Do not replace it with a shell
  invocation.
