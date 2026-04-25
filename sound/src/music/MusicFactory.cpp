#include "music/MusicFactory.hpp"

#include "music/YouTubeMusicProvider.hpp"

#include <iostream>

namespace hecquin::music {

namespace {

std::unique_ptr<MusicProvider> make_youtube(const MusicConfig& cfg) {
    YouTubeMusicConfig ycfg;
    ycfg.yt_dlp_binary  = cfg.yt_dlp_binary;
    ycfg.ffmpeg_binary  = cfg.ffmpeg_binary;
    ycfg.cookies_file   = cfg.cookies_file;
    if (cfg.sample_rate_hz > 0) ycfg.sample_rate_hz = cfg.sample_rate_hz;
    ycfg.apply_env_overrides();
    return std::make_unique<YouTubeMusicProvider>(std::move(ycfg));
}

} // namespace

std::unique_ptr<MusicProvider> make_provider_from_config(const MusicConfig& cfg) {
    if (cfg.provider == "youtube" || cfg.provider.empty()) {
        return make_youtube(cfg);
    }
    std::cerr << "[music] unknown provider \"" << cfg.provider
              << "\" — falling back to YouTube Music." << std::endl;
    return make_youtube(cfg);
}

} // namespace hecquin::music
