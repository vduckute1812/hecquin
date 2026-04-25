# `music/`

Music search + streaming playback for the "open music" voice intent.
Activated when `LocalIntentMatcher` resolves `open music` and the
listener enters `ListenerMode::Music`; the next utterance is forwarded
to `MusicSession::handle` as a song query.

## Files

| File | Purpose |
|---|---|
| `MusicProvider.hpp` | Interface: `search(query) -> optional<MusicTrack>`, `play(track) -> bool`, `stop()`. |
| `YouTubeMusicProvider.hpp/cpp` | Default provider. Shells out to `yt-dlp` + `ffmpeg`, pipes raw mono int16 PCM into `tts::playback::StreamingSdlPlayer`. Optional `HECQUIN_YT_COOKIES_FILE` for YouTube Premium auth. |
| `MusicSession.hpp/cpp` | Facade that wraps a `MusicProvider` + `AudioCapture::MuteGuard`. Returns a `MusicPlayback` `Action` regardless of outcome so the listener can exit `Music` mode cleanly. |
| `MusicFactory.hpp/cpp` | Builds a `MusicProvider` from `AppConfig::music`. Unknown `provider` values fall back to YouTube with a warning. |

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

## Known limitations

- `play()` is synchronous — mid-song voice cancellation is blocked by the
  mic-mute `MuteGuard`. SIGINT still cleanly aborts the child pipeline.
- "Stop music" only works *before* a song starts streaming (between
  `open music` and the song name). A non-blocking variant with a watcher
  thread is a planned follow-up.
