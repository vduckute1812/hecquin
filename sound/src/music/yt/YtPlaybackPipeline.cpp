#include "music/yt/YtPlaybackPipeline.hpp"

#include "common/Subprocess.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace hecquin::music::yt {

bool YtPlaybackPipeline::run(const std::string& shell_command,
                             int sample_rate_hz,
                             std::atomic<int>* child_pid_out,
                             const std::atomic<bool>& aborted) {
    auto sp = hecquin::common::Subprocess::spawn_read(shell_command);
    if (!sp.valid()) return false;
    if (child_pid_out) child_pid_out->store(sp.pid());

    // Streaming player drives SDL.  Construct fresh per track so we can
    // tear it down cleanly after each song (SDL keeps the device handle).
    player_ = std::make_unique<hecquin::tts::playback::StreamingSdlPlayer>();
    if (!player_->start(sample_rate_hz)) {
        std::cerr << "[music] failed to open SDL audio device" << std::endl;
        sp.kill_and_reap();
        if (child_pid_out) child_pid_out->store(0);
        player_.reset();
        return false;
    }

    bool got_audio = false;
    std::array<std::int16_t, 4096> samples{};
    // Read raw s16le from the ffmpeg stdout end of the pipeline in
    // fixed-size chunks and push into the player.  Short reads are fine
    // as long as they're a multiple of sizeof(int16_t) — on POSIX that
    // is guaranteed for blocking read on a pipe.
    for (;;) {
        const long bytes = sp.read_some(
            samples.data(), samples.size() * sizeof(std::int16_t));
        if (bytes < 0) break;
        if (bytes == 0) break;
        const std::size_t n =
            static_cast<std::size_t>(bytes) / sizeof(std::int16_t);
        if (n == 0) continue;
        got_audio = true;
        player_->push(samples.data(), n);
        if (aborted.load()) break;
    }

    sp.kill_and_reap();
    if (child_pid_out) child_pid_out->store(0);

    player_->finish();
    player_->wait_until_drained();
    player_->stop();
    player_.reset();

    return got_audio && !aborted.load();
}

void YtPlaybackPipeline::set_paused(bool paused) {
    // SDL_PauseAudioDevice (the call inside `set_paused`) is documented
    // as thread-safe; the only race window is `player_` being torn down
    // by `run()` returning and `~unique_ptr` running.  Best-effort null
    // check matches the original `YouTubeMusicProvider::pause()`.
    if (player_) player_->set_paused(paused);
}

void YtPlaybackPipeline::set_gain_target(float linear, int ramp_ms) {
    // Same lifetime caveat as `set_paused`: the call between the
    // null check and the dispatch is a tiny window where `player_`
    // could be torn down by `run()` returning.  In practice the
    // listener thread invokes both, so the race is bounded to a
    // single song's tail.
    if (player_) player_->set_gain_target(linear, ramp_ms);
}

void YtPlaybackPipeline::finish_now() {
    if (player_) {
        player_->finish();
        player_->stop();
    }
}

} // namespace hecquin::music::yt
