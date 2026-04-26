#pragma once

#include "music/YouTubeMusicProvider.hpp"

#include <string>

/**
 * Pure command-string builders for the yt-dlp / ffmpeg pipeline.
 *
 * Splitting the shell-string assembly out of `YouTubeMusicProvider`
 * gives us:
 *   - one place for the long-standing TAB-vs-`\t` quirk in `--print`,
 *   - a unit-testable surface that doesn't fork any subprocesses,
 *   - a natural extension point if a future back-end (apple_music?)
 *     wants to share the cookies / sample-rate plumbing.
 *
 * Every builder returns a string suitable for `/bin/sh -c`.  All
 * substitution points pass through `hecquin::common::posix_sh_single_quote`
 * so user-controlled inputs (queries, URLs, paths) cannot inject shell
 * metacharacters.
 */
namespace hecquin::music::yt {

/**
 * `yt-dlp --print %(title)s\t%(webpage_url)s` for the top hit of
 * `query`.  The query is wrapped with `ytsearch1:` and biased toward
 * songs by appending " music".
 */
std::string build_search_command(const YouTubeMusicConfig& cfg,
                                 const std::string& query);

/**
 * `yt-dlp -f bestaudio -o - <url> | ffmpeg ... -f s16le -ac 1 -ar
 * <rate> pipe:1`.  The output of `/bin/sh -c <command>` is raw mono
 * `int16` PCM.
 */
std::string build_playback_command(const YouTubeMusicConfig& cfg,
                                   const std::string& url);

} // namespace hecquin::music::yt
