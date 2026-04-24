# `tts/wav/`

Generic 16-bit / mono WAV reader. Was previously exported from
`PiperSpeech.hpp` even though it has nothing to do with Piper — moved
here so it can be used anywhere (and not leak through the TTS public
API).

## Files

| File | Purpose |
|---|---|
| `WavReader.hpp/cpp` | `read_pcm_s16_mono(path, samples_out, sample_rate_out) → bool` and `parse_sample_rate(header_bytes)` — strict 44-byte PCM WAV parsing with trivial error reporting via return code. |

## Callers

- `tts/backend/PiperShellBackend` — reads the `/tmp/…wav` that legacy
  Piper writes.
- (Future) anywhere else that needs to load a small WAV for tests or
  fixtures.

## Notes

- Only the subset actually used by the shell backend is implemented: PCM,
  16-bit, mono, 22050 Hz (but the code reads the rate out of the header
  — do not hardcode).
- If you need multi-channel / 24-bit / RIFX support, add a new reader
  here rather than inlining parsing in callers.
