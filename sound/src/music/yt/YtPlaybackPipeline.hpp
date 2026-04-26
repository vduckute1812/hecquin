#pragma once

#include "music/MusicProvider.hpp"
#include "tts/playback/StreamingSdlPlayer.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace hecquin::music::yt {

/**
 * Owns one playback's worth of state: the `yt-dlp | ffmpeg` subprocess
 * pipeline and the `StreamingSdlPlayer` that drains its stdout.
 *
 * Lifted out of `YouTubeMusicProvider::play` so the provider class can
 * focus on lifecycle / abort semantics.  One pipeline instance handles
 * exactly one track; `run()` is blocking and returns when the pipeline
 * drains naturally, when `request_abort()` is observed, or when the
 * SDL device fails to open.
 *
 * The `child_pid_out` / `aborted` parameters let the enclosing provider
 * observe the spawned child's pid (so external `stop()` can SIGTERM it
 * without taking a lock on the pipeline) and signal an abort
 * cooperatively.
 */
class YtPlaybackPipeline {
public:
    /**
     * Run one playback to completion.
     *
     * @param shell_command  the `/bin/sh -c` string built by
     *                       `build_playback_command`.
     * @param sample_rate_hz mono PCM rate matching the ffmpeg `-ar`.
     * @param child_pid_out  set to the spawned child's pid as soon as
     *                       the subprocess is up; cleared back to 0
     *                       before `run()` returns.  May be null.
     * @param aborted        cooperatively polled inside the read loop.
     *                       `request_abort()` flips it externally.
     * @return true iff at least one PCM sample was decoded and the
     *         loop wasn't aborted mid-stream.
     */
    bool run(const std::string& shell_command,
             int sample_rate_hz,
             std::atomic<int>* child_pid_out,
             const std::atomic<bool>& aborted);

    /** Best-effort SDL pause toggles forwarded to the active player. */
    void set_paused(bool paused);

    /**
     * Force the player to drain immediately.  Called by the enclosing
     * provider's `stop()` to break out of the read loop early without
     * waiting for the subprocess pipe to close.
     */
    void finish_now();

private:
    std::unique_ptr<hecquin::tts::playback::StreamingSdlPlayer> player_;
};

} // namespace hecquin::music::yt
