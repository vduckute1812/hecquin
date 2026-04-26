# `tts/runtime/`

One-time Piper runtime configuration. Today this is just the macOS
dylib-path workaround, but the seam exists so future runtime knobs
(Linux `LD_LIBRARY_PATH`, Raspberry Pi `LD_PRELOAD`, …) land here
instead of in `PiperSpeech.cpp`.

## Files

| File | Purpose |
|---|---|
| `PiperRuntime.hpp/cpp` | `configure()` — idempotent, called before the first Piper invocation. On macOS prepends the bundled Piper install dir to `DYLD_FALLBACK_LIBRARY_PATH` so the shared libraries resolve without a global install. No-op elsewhere. The body is wrapped in `std::call_once` so the second-and-onwards spawns are zero-cost (the underlying filesystem probes never change after the first call). |

## Notes

- Keep `configure()` idempotent; we call it from every synthesis path on
  purpose so a late-bound CLI can still reach the dylibs. Internally a
  `std::call_once` flag short-circuits the body after the first call.
- Do **not** `setenv` anything else from this TU. Environment mutation
  has cross-thread ordering traps — add a new knob only when there is a
  dylib-loading problem you cannot fix at the build-system layer.
