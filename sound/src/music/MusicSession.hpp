#pragma once

#include "actions/Action.hpp"
#include "music/MusicProvider.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace hecquin::music {

/**
 * Facade the voice pipeline talks to once the user has said a song name
 * in `ListenerMode::Music`, and through which mid-song voice commands
 * (`stop / pause / continue music`) reach the underlying provider.
 *
 * Lifecycle of one song:
 *
 *   1. `handle(query)` searches synchronously (cheap; one yt-dlp call)
 *      and, on a hit, dispatches `provider_.play(track)` to a private
 *      background thread.  The method returns a "Now playing …"
 *      `Action` immediately so the listener's poll loop is freed up
 *      and can keep capturing voice while audio streams.
 *   2. While the song streams, `abort()` / `pause()` / `resume()` may
 *      be invoked from the listener thread to control playback.
 *   3. When `play()` returns (drained or aborted) the background
 *      thread exits; the next `handle()` / `abort()` / `~MusicSession`
 *      call joins it.
 *
 * The microphone is no longer paused around `play()` — that's why
 * `capture` is dropped from the dependency list.  The voice pipeline's
 * primary VAD picks up "stop music" through speaker bleed; the small
 * intent regex set means stray transcription of song lyrics rarely
 * matches anything.  Tests inject a fake provider and never see the
 * mic.
 */
class MusicSession {
public:
    explicit MusicSession(MusicProvider& provider) : provider_(provider) {}
    ~MusicSession();

    MusicSession(const MusicSession&)            = delete;
    MusicSession& operator=(const MusicSession&) = delete;

    /**
     * Handle one user-provided song query.  Returns the `Action` the
     * listener should announce immediately:
     *   - empty / unparsable query → `MusicPlayback(ok=false)`.
     *   - search miss              → `MusicPlayback(ok=false)`.
     *   - search hit               → `MusicPlayback(ok=true, title)`,
     *                                playback running on a background
     *                                thread.
     */
    Action handle(const std::string& query);

    /** Stop any in-flight playback and join the worker thread.  Safe to
     *  call when nothing is playing.  Idempotent. */
    void abort();

    /** Best-effort: forward to `provider_.pause()`.  No-op when no song
     *  is currently playing. */
    void pause();

    /** Best-effort counterpart to `pause()`. */
    void resume();

    /** Persistent user-volume nudge ("a little louder"); forwarded to
     *  `provider_.step_volume()`.  No-op if nothing is playing. */
    void step_volume(float delta);

    /** Skip to the next track.  Single-track providers fall back to
     *  `abort()` (which gracefully tears down the worker thread). */
    void skip();

    /** True between `handle()` returning a successful playback action
     *  and the background thread observing `provider_.play()` return. */
    bool is_playing() const { return playing_.load(); }

private:
    void abort_locked_();

    /**
     * Aborts any in-flight song, then spawns the background playback
     * thread for `track` while holding `thread_mu_`.  Called from
     * `handle()` once we know we have a hit.
     */
    void start_playback_thread_(const MusicTrack& track);

    MusicProvider&    provider_;
    std::mutex        thread_mu_;
    std::thread       playback_thread_;
    std::atomic<bool> playing_{false};
};

} // namespace hecquin::music
