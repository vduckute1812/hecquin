#pragma once

#include <optional>
#include <string>

namespace hecquin::music {

/**
 * A single resolved track: what to announce and the URL a provider can
 * hand to its own streaming pipeline.  Keeping it provider-agnostic lets
 * `MusicSession` log / narrate without knowing whether the URL points at
 * YouTube, Apple Music, or anything else.
 */
struct MusicTrack {
    std::string title;
    std::string url;
};

/**
 * Back-end interface for music search + playback.
 *
 * Implementations currently: `YouTubeMusicProvider` (shells out to yt-dlp
 * + ffmpeg and streams PCM through `StreamingSdlPlayer`).  Apple Music and
 * any other provider should plug in here so `MusicSession` and the
 * listener wiring stay unchanged.
 *
 * Contract:
 *   - `search()` returns the top candidate for `query` (or `nullopt` if
 *     the provider couldn't resolve anything).  Must NOT play audio.
 *   - `play()` is blocking — it should not return until playback has
 *     drained or been aborted.  Must be safe to call with the microphone
 *     muted by a `AudioCapture::MuteGuard` in the caller.
 *   - `stop()` may be invoked concurrently from another thread to break
 *     `play()` early (e.g. on SIGINT).  Idempotent.
 */
class MusicProvider {
public:
    virtual ~MusicProvider() = default;

    virtual std::optional<MusicTrack> search(const std::string& query) = 0;
    virtual bool play(const MusicTrack& track) = 0;
    virtual void stop() = 0;
};

} // namespace hecquin::music
