#pragma once

#include "music/MusicProvider.hpp"

#include <optional>
#include <string_view>

/**
 * Pure parser for the textual output of
 *   `yt-dlp --print '%(title)s\t%(webpage_url)s' ...`
 *
 * Lifted out of `YouTubeMusicProvider::search` so the parsing logic is
 * exercisable without forking yt-dlp.  Two real-world quirks are
 * accepted:
 *   1. yt-dlp may print progress / warning lines before the actual
 *      `--print` payload — the parser scans for the first non-empty
 *      line.
 *   2. Some yt-dlp builds historically emitted the literal two-byte
 *      sequence `\t` instead of a real TAB; we fall back to that
 *      separator only after a real TAB miss.
 *
 * Returns `nullopt` for empty input, missing separator, or empty URL.
 */
namespace hecquin::music::yt {

std::optional<MusicTrack> parse_search_output(std::string_view raw);

} // namespace hecquin::music::yt
