#include "music/YouTubeMusicProvider.hpp"

#include "common/EnvParse.hpp"
#include "common/Subprocess.hpp"
#include "music/yt/YtDlpCommands.hpp"
#include "music/yt/YtDlpSearch.hpp"
#include "music/yt/YtPlaybackPipeline.hpp"

#include <signal.h>

#include <algorithm>
#include <array>
#include <string>

namespace hecquin::music {

void YouTubeMusicConfig::apply_env_overrides() {
    namespace env = hecquin::common::env;
    if (const char* v = env::read_string("HECQUIN_YT_DLP_BIN"))      yt_dlp_binary = v;
    if (const char* v = env::read_string("HECQUIN_FFMPEG_BIN"))      ffmpeg_binary = v;
    if (const char* v = env::read_string("HECQUIN_YT_COOKIES_FILE")) cookies_file  = v;
    int rate = 0;
    if (env::parse_int("HECQUIN_MUSIC_SAMPLE_RATE", rate)) {
        sample_rate_hz = std::max(8000, rate);
    }
}

YouTubeMusicProvider::YouTubeMusicProvider(YouTubeMusicConfig cfg)
    : cfg_(std::move(cfg)) {}

YouTubeMusicProvider::~YouTubeMusicProvider() {
    stop();
}

std::optional<MusicTrack>
YouTubeMusicProvider::search(const std::string& query) {
    if (query.empty()) return std::nullopt;

    const std::string cmd = yt::build_search_command(cfg_, query);
    auto sp = hecquin::common::Subprocess::spawn_read(cmd);
    if (!sp.valid()) return std::nullopt;

    std::string buf;
    std::array<char, 1024> chunk{};
    for (;;) {
        const long n = sp.read_some(chunk.data(), chunk.size());
        if (n <= 0) break;
        buf.append(chunk.data(), static_cast<std::size_t>(n));
    }
    sp.kill_and_reap();

    return yt::parse_search_output(buf);
}

bool YouTubeMusicProvider::play(const MusicTrack& track) {
    if (track.url.empty()) return false;
    aborted_.store(false);

    pipeline_ = std::make_unique<yt::YtPlaybackPipeline>();
    const std::string cmd = yt::build_playback_command(cfg_, track.url);
    const bool ok = pipeline_->run(cmd, cfg_.sample_rate_hz,
                                   &child_pid_, aborted_);
    pipeline_.reset();
    return ok;
}

void YouTubeMusicProvider::stop() {
    aborted_.store(true);
    const int pid = child_pid_.exchange(0);
    if (pid > 0) {
        ::kill(pid, SIGTERM);
    }
    if (pipeline_) {
        pipeline_->finish_now();
    }
}

void YouTubeMusicProvider::pause() {
    if (pipeline_) pipeline_->set_paused(true);
}

void YouTubeMusicProvider::resume() {
    if (pipeline_) pipeline_->set_paused(false);
}

} // namespace hecquin::music
