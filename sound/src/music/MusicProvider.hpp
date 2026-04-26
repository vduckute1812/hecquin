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
 *     drained or been aborted.  `MusicSession` runs it on a background
 *     thread so the listener can keep capturing voice while audio
 *     streams.
 *   - `stop()` may be invoked concurrently from another thread to break
 *     `play()` early (e.g. on SIGINT or a "stop music" intent).
 *     Idempotent.
 *   - `pause()` / `resume()` are best-effort.  The default implementation
 *     is a no-op so providers that can't suspend their pipeline (e.g. a
 *     hypothetical streamer that has no SDL device of its own) still
 *     compile without ceremony — callers should treat them as advisory.
 */
class MusicProvider {
public:
    virtual ~MusicProvider() = default;

    virtual std::optional<MusicTrack> search(const std::string& query) = 0;
    virtual bool play(const MusicTrack& track) = 0;
    virtual void stop() = 0;
    /** Best-effort: suspend the audio device without tearing down the
     *  decoder pipeline.  Default no-op. */
    virtual void pause() {}
    /** Best-effort: counterpart to `pause()`.  Default no-op. */
    virtual void resume() {}
    /**
     * Best-effort: set the linear output gain on the underlying audio
     * device.  Used by the barge-in controller to duck the music when
     * the user starts speaking.  Default no-op for providers that
     * have no concept of post-fader gain (they can implement it later
     * without changing callers).  Non-virtual / unscoped intentionally
     * — providers that opt in override; everything else stays silent.
     *
     * @param linear  0 = silent, 1 = unattenuated, >1 = boosted.
     * @param ramp_ms cross-fade duration to avoid clicks.
     */
    virtual void set_gain_target(float linear, int ramp_ms) {
        (void)linear; (void)ramp_ms;
    }
};

} // namespace hecquin::music
