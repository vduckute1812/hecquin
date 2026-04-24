# `cli/`

Entry points for the non-learning executables.

## Files

| File | Produces | Purpose |
|---|---|---|
| `TextToSpeech.cpp` | `text_to_speech` binary | Tiny CLI around `piper_speak_and_play`. Parses `-m <model>` / `-o <wav>` / `-h`, reads the utterance from `argv`, and either plays it through SDL or writes a 16-bit-PCM WAV. |

The `voice_detector` entry point lives in [`../voice/VoiceDetector.cpp`](../voice/)
and the learning binaries live in [`../learning/cli/`](../learning/cli/README.md).

## Conventions

- Keep the `main()` body thin: argument parsing + construct the facade +
  delegate. Anything substantial belongs in `tts/`, `voice/`, or
  `learning/`.
- Do **not** add a third executable here — prefer a new `cli/` folder
  inside the owning package (same as `learning/cli/` and `voice/`).
