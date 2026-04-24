# `tts/playback/`

SDL audio device + two player policies that sit on top of it. Separating
lifecycle from the playback strategy keeps the facade free of producer /
consumer plumbing.

## Files

| File | Purpose |
|---|---|
| `SdlAudioDevice.hpp/cpp` | Open / close / re-open the SDL audio device at the requested sample rate. Owns `SDL_AudioDeviceID` and keeps a bool so the device is not re-opened needlessly across utterances. |
| `BufferedSdlPlayer.hpp/cpp` | One-shot pre-buffered playback: queue all PCM at once, wait for drain, return. Used for the drill reference audio (we already have the full buffer). |
| `StreamingSdlPlayer.hpp/cpp` | Producer / consumer ring: SDL callback drains chunks until EOF is signalled. Playback starts as soon as the first chunk is pushed, so long replies don't stall the mic re-open. Used for `piper_speak_and_play_streaming`. |

## Notes

- The playback sample rate (22050 Hz) comes from Piper — do not hardcode
  it at the call site. Pass the rate returned by the backend into the
  player.
- `StreamingSdlPlayer` must be drained before the capture device is
  unpaused; the `MuteGuard` in `voice/TtsResponsePlayer` owns that
  invariant.
