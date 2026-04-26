#include "music/yt/YtDlpCommands.hpp"

#include "common/ShellEscape.hpp"

#include <sys/stat.h>

#include <sstream>
#include <string>

namespace hecquin::music::yt {

namespace {

using hecquin::common::posix_sh_single_quote;

bool file_exists(const std::string& path) {
    if (path.empty()) return false;
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

// `--cookies <file>` fragment, already shell-safe; empty if no cookies
// file is configured or the path doesn't exist on disk.  Missing
// cookies files are silently downgraded to "no auth" rather than
// failing the whole search — yt-dlp still works without them.
std::string cookies_arg(const std::string& cookies_file) {
    if (!file_exists(cookies_file)) return {};
    return " --cookies " + posix_sh_single_quote(cookies_file);
}

} // namespace

std::string build_search_command(const YouTubeMusicConfig& cfg,
                                 const std::string& query) {
    // `ytsearch1:` = top 1 hit.  Appending "music" biases YouTube's own
    // relevance ranker towards songs vs talks / video essays, which
    // matches user intent ("open music → song name").
    const std::string q = "ytsearch1:" + query + " music";

    // NOTE: the `--print` template MUST contain a real TAB byte (0x09).
    // yt-dlp does not expand backslash escapes inside `--print`, so writing
    // `'%(title)s\t%(webpage_url)s'` ships a literal `\` `t` to yt-dlp and
    // we never find a tab to split on.  Embed the tab via the single-
    // quote escaper, which preserves bytes verbatim across the quote.
    const std::string print_fmt =
        std::string("%(title)s") + '\t' + "%(webpage_url)s";

    std::ostringstream cmd;
    cmd << posix_sh_single_quote(cfg.yt_dlp_binary)
        << " --no-warnings --no-playlist"
        << " --default-search ytsearch"
        << " --print " << posix_sh_single_quote(print_fmt)
        << cookies_arg(cfg.cookies_file)
        << " " << posix_sh_single_quote(q);
    return cmd.str();
}

std::string build_playback_command(const YouTubeMusicConfig& cfg,
                                   const std::string& url) {
    std::ostringstream cmd;
    cmd << posix_sh_single_quote(cfg.yt_dlp_binary)
        << " -q --no-warnings --no-playlist -f bestaudio -o -"
        << cookies_arg(cfg.cookies_file)
        << " " << posix_sh_single_quote(url)
        << " | " << posix_sh_single_quote(cfg.ffmpeg_binary)
        << " -hide_banner -loglevel error -i pipe:0"
        << " -f s16le -ac 1 -ar " << cfg.sample_rate_hz
        << " pipe:1";
    return cmd.str();
}

} // namespace hecquin::music::yt
