# `tts/`

Piper TTS facade + the split collaborators under `backend/`, `playback/`,
`runtime/`, and `wav/`. The public C-style API (`piper_speak_and_play`,
`piper_speak_and_play_streaming`, `piper_synthesize_to_buffer`) stays
intact so every caller is unchanged; implementation details now live in
single-responsibility files.

## Files

| File | Purpose |
|---|---|
| `PiperSpeech.hpp/cpp` | Public facade. Composes a default `IPiperBackend` (pipe → shell fallback) with a `BufferedSdlPlayer` / `StreamingSdlPlayer` depending on the call. Keeps the legacy free-function API. |

## Sub-folders

- [`backend/`](./backend/README.md) — Strategy interface + pipe / shell / composite-fallback implementations and the `PiperSpawn` Template Method.
- [`playback/`](./playback/README.md) — SDL audio device lifecycle + pre-buffered and streaming players.
- [`runtime/`](./runtime/README.md) — macOS `DYLD_FALLBACK_LIBRARY_PATH` configuration.
- [`wav/`](./wav/README.md) — Generic 44-byte WAV reader (no Piper dependency).

## Who calls what

- `voice/TtsResponsePlayer` — `piper_speak_and_play_streaming` for assistant / tutor replies.
- `learning/pronunciation/drill/DrillReferenceAudio` — `piper_synthesize_to_buffer` (needs raw PCM to compute pitch contour) + `sdl_play_s16_mono_22k`.
- `cli/TextToSpeech.cpp` — `piper_speak_and_play` (plus WAV-write path).

## Tests

- `tests/test_piper_backend_fallback.cpp` — Strategy composition with stubbed backends.

## Notes

- Keep the facade's C-style API stable; callers outside this folder must
  never include `tts/backend/*` directly.
- The streaming player exists so long replies start playing as soon as
  the first chunk arrives. Do not "fix" it to buffer — that regresses
  perceived latency by ~600 ms on typical tutor replies.
