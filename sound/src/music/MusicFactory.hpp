#pragma once

#include "config/AppConfig.hpp"
#include "music/MusicProvider.hpp"

#include <memory>

namespace hecquin::music {

/**
 * Build a `MusicProvider` from `AppConfig::music`.
 *
 * Current providers:
 *   - "youtube" (default): `YouTubeMusicProvider` — yt-dlp + ffmpeg.
 *
 * Unknown names fall back to YouTube with a warning so a bad env var
 * does not silently break music playback.  Adding a new back-end (e.g.
 * Apple Music) is a one-branch extension here.
 */
std::unique_ptr<MusicProvider> make_provider_from_config(const MusicConfig& cfg);

} // namespace hecquin::music
