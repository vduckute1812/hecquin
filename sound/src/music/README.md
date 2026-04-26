# `music/`

Music search + streaming playback for the "open music" voice intent.
Activated when `LocalIntentMatcher` resolves `open music` and the
listener enters `ListenerMode::Music`; the next utterance is forwarded
to `MusicSession::handle` as a song query.

## Files

| File | Purpose |
|---|---|
| `MusicProvider.hpp` | Interface: `search`, `play` (blocking), `stop`, plus advisory `pause` / `resume` (default no-op). |
| `YouTubeMusicProvider.hpp/cpp` | Thin orchestrator over `yt/`. Builds the search command via `yt::build_search_command`, parses results with `yt::parse_search_output`, and delegates one playback to `yt::YtPlaybackPipeline`. Optional `HECQUIN_YT_COOKIES_FILE` for YouTube Premium auth. `pause / resume` toggle the SDL device. |
| `yt/YtDlpCommands.hpp/cpp` | Pure command-string builders (`build_search_command`, `build_playback_command`). No I/O — exercised by `tests/music/test_yt_dlp_commands.cpp`. |
| `yt/YtDlpSearch.hpp/cpp` | Pure `parse_search_output(stdout) → optional<MusicTrack>`. Handles TAB / `\\t` separators and skips blank preamble lines from `yt-dlp`. Tested by `tests/music/test_yt_dlp_search_parser.cpp`. |
| `yt/YtPlaybackPipeline.hpp/cpp` | Owns the read loop + `StreamingSdlPlayer` lifecycle for one playback. Builds and manages the `yt-dlp \| ffmpeg` subprocess via `common::Subprocess`. |
| `MusicSession.hpp/cpp` | Async facade. Searches synchronously, then dispatches `provider.play()` to a private `std::thread`. Exposes `abort / pause / resume` for the mid-song voice intents. The microphone is **not** muted during playback so "stop / pause / continue music" can be heard over the song. The thread-lifecycle plumbing (abort prior song → mark playing → spawn → mark done) is factored out into the private `start_playback_thread_(track)` so `handle()` reads as: log → resolve → either build "not found" reply or `start_playback_thread_(track)` + build "playing" reply. |
| `MusicFactory.hpp/cpp` | Builds a `MusicProvider` from `AppConfig::music`. Unknown `provider` values fall back to YouTube with a warning. |

Wiring into the `VoiceListener` is centralised in
[`../voice/MusicWiring.hpp`](../voice/MusicWiring.hpp) — call
`hecquin::voice::install_music_wiring(listener, cfg.music)` instead of
hand-rolling the provider + session + 4 callbacks in every executable
main.

## Adding a new provider (e.g. Apple Music)

1. Implement `MusicProvider` in a new `AppleMusicProvider.hpp/.cpp`.
2. Teach `MusicFactory::make_provider_from_config` a new branch on
   `cfg.provider == "apple"`.
3. Add any extra config fields to `MusicConfig` in
   [`config/AppConfig.hpp`](../config/AppConfig.hpp).

Listener / router / matcher wiring does **not** change — they talk only
to the interface via `MusicSession`.

## Runtime dependencies

- `yt-dlp` on `$PATH` (or `HECQUIN_YT_DLP_BIN`).
- `ffmpeg` on `$PATH` (or `HECQUIN_FFMPEG_BIN`).
- SDL2 (already required by the rest of the sound module).

## Voice control while a song is streaming

`MusicSession::handle()` is non-blocking: search runs on the listener
thread, then `provider.play()` is dispatched to a private worker thread
and `handle()` returns the "Now playing …" `Action` immediately.  The
listener drops back to its home mode, the microphone stays live, and
the `LocalIntentMatcher` fast-path keeps watching for these intents:

| Phrase                                       | `ActionKind`   | Effect |
|---|---|---|
| `stop / cancel / exit / close / end music`   | `MusicCancel`  | `MusicSession::abort()` → `provider.stop()` → worker joins. |
| `pause music`                                | `MusicPause`   | `provider.pause()` (best-effort; SDL device pauses). |
| `continue / resume / unpause music`          | `MusicResume`  | `provider.resume()` — counterpart of pause. |

Tradeoff: speakers bleed into the mic.  The intent matcher only acts on
that small phrase set, so stray transcription of song lyrics rarely
triggers anything; anything else falls through to the chat fallback as
usual.

To stop the speaker bleed from poisoning the adaptive VAD, the listener
flips `UtteranceCollector::set_external_audio_active(true)` for the
lifetime of a song (i.e. between `MusicPlayback` and
`MusicCancel / MusicPause`) via the small `voice/MusicSideEffects`
collaborator that owns the abort / pause / resume callbacks plus a
non-owning collector pointer.  The companion `MusicNotFound` action
fires on a search miss and is treated like `MusicPlayback` for
mode-exit purposes but **without** engaging the bleed gate, since no
audio is actually playing.  The noise-floor tracker stops absorbing
RMS samples while the song streams, then `reset_noise_floor()` on cancel
forces a fresh calibration so the start threshold returns to a level
that detects normal speech.  Without this gate, "stop music" would
correctly abort the song but leave the floor sitting at the music's RMS,
making the very next sentence inaudible to the VAD.

## Known limitations

- Streaming failures *after* `handle()` returns are silent to the user
  (logged to stderr only).  The "Now playing …" reply has already been
  spoken by then; a failed song just shortens itself.
- `pause / resume` rely on the provider implementing them.  The default
  `MusicProvider` interface exposes them as no-ops so future back-ends
  that can't suspend their pipeline still compile.
