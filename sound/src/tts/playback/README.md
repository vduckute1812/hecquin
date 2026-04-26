# `tts/playback/`

SDL audio device + two player policies that sit on top of it. Separating
lifecycle from the playback strategy keeps the facade free of producer /
consumer plumbing.

## Files

| File | Purpose |
|---|---|
| `SdlAudioDevice.hpp/cpp` | Open / close / re-open the SDL audio device at the requested sample rate. Owns `SDL_AudioDeviceID` and keeps a bool so the device is not re-opened needlessly across utterances. Used by the buffered player. |
| `SdlMonoDevice.hpp/cpp` | RAII wrapper around the SDL mono-int16 device used by the streaming player. Lifecycle only — no buffer state or signalling lives here. |
| `PcmRingQueue.hpp/cpp` | Mutex + `std::condition_variable` + `std::deque<int16_t>` + `eof_` flag. Replaces the busy-sleep drain that lived in the old `StreamingSdlPlayer`. Methods: `push`, `pop_into(uint8_t* dst, int len)` (drain into the SDL callback buffer), `mark_eof`, `wait_until_drained()`. Tested in isolation by `tests/tts/test_pcm_ring_queue.cpp`. |
| `BufferedSdlPlayer.hpp/cpp` | One-shot pre-buffered playback: queue all PCM at once, wait for drain, return. Used for the drill reference audio (we already have the full buffer). |
| `StreamingSdlPlayer.hpp/cpp` | Thin facade composing `SdlMonoDevice` + `PcmRingQueue`. Playback starts as soon as the first chunk is pushed, so long replies don't stall the mic re-open. Used for `piper_speak_and_play_streaming`. Also exposes `set_gain_target(linear, ramp_ms)` (lock-free, audio-callback-slewed) so `voice::AudioBargeInController` can duck the music output when the user starts speaking or when the assistant is talking over a song. The pure helper `apply_gain(...)` is static + side-effect-free for unit tests. |

## Notes

- The playback sample rate (22050 Hz) comes from Piper — do not hardcode
  it at the call site. Pass the rate returned by the backend into the
  player.
- `StreamingSdlPlayer` must be drained before the capture device is
  unpaused; the `MuteGuard` in `voice/TtsResponsePlayer` owns that
  invariant.
- The gain ramp (`set_gain_target`) is intentionally driven from the
  SDL audio callback — no extra threads. A missed target on one buffer
  is corrected on the next, so the listener can call it freely from
  the poll loop. See [`../../../UX_FLOW.md`](../../../UX_FLOW.md) for
  the TTS-ducks-music timing.
