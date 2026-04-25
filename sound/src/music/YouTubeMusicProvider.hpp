#pragma once

#include "music/MusicProvider.hpp"
#include "tts/playback/StreamingSdlPlayer.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <string>

namespace hecquin::music {

/** Runtime configuration for the YouTube Music back-end. */
struct YouTubeMusicConfig {
    /** Absolute path to `yt-dlp`; "" = look up on $PATH. */
    std::string yt_dlp_binary = "yt-dlp";
    /** Absolute path to `ffmpeg`; "" = look up on $PATH. */
    std::string ffmpeg_binary = "ffmpeg";
    /**
     * Optional Netscape-format cookies file (exported from a browser that
     * is signed into a YouTube Premium Google account).  Empty = no
     * authentication (ads + reduced quality; still works for search).
     */
    std::string cookies_file;
    /** Mono PCM sample rate handed to `StreamingSdlPlayer`. */
    int sample_rate_hz = 44100;

    /** Populate from `HECQUIN_YT_DLP_BIN` / `HECQUIN_FFMPEG_BIN` /
     *  `HECQUIN_YT_COOKIES_FILE` / `HECQUIN_MUSIC_SAMPLE_RATE`.  Unset
     *  vars leave the field untouched so compile-time defaults remain. */
    void apply_env_overrides();
};

/**
 * `yt-dlp` + `ffmpeg` powered back-end.  `search()` runs a single
 * `ytsearch1:` query and parses the title / URL.  `play()` spawns a
 * subprocess pipeline that pipes bestaudio through ffmpeg decoding to
 * raw mono int16 PCM and pumps it into `StreamingSdlPlayer`.
 *
 * Auth: if `cookies_file` is non-empty and exists, both subprocesses get
 * `--cookies <path>` (yt-dlp) so a signed-in Premium account can skip
 * ads and get higher bitrate.
 */
class YouTubeMusicProvider final : public MusicProvider {
public:
    explicit YouTubeMusicProvider(YouTubeMusicConfig cfg = {});
    ~YouTubeMusicProvider() override;

    YouTubeMusicProvider(const YouTubeMusicProvider&) = delete;
    YouTubeMusicProvider& operator=(const YouTubeMusicProvider&) = delete;

    std::optional<MusicTrack> search(const std::string& query) override;
    bool play(const MusicTrack& track) override;
    void stop() override;

    const YouTubeMusicConfig& config() const { return cfg_; }

private:
    YouTubeMusicConfig cfg_;
    /**
     * PID of the currently-running `yt-dlp | ffmpeg` pipeline, or 0 when
     * idle.  Stored atomically so `stop()` from SIGINT / another thread
     * can send SIGTERM without a race.
     */
    std::atomic<int> child_pid_{0};
    std::atomic<bool> aborted_{false};
    std::unique_ptr<hecquin::tts::playback::StreamingSdlPlayer> player_;
};

} // namespace hecquin::music
