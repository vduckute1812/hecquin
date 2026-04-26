#include "music/yt/YtDlpSearch.hpp"

#include <iostream>
#include <string>

namespace hecquin::music::yt {

std::optional<MusicTrack> parse_search_output(std::string_view raw) {
    // Keep the first non-empty line only; yt-dlp occasionally prints
    // progress hints before the `--print` payload.
    std::string line;
    for (char c : raw) {
        if (c == '\n') {
            if (!line.empty()) break;
        } else {
            line.push_back(c);
        }
    }
    if (line.empty()) return std::nullopt;

    // Primary separator is a real TAB; fall back to a literal "\\t" only
    // to survive yt-dlp regressions where escapes aren't expanded.  A
    // line with no separator at all is logged once and then dropped, so
    // silent parse misses can't masquerade as "no match".
    auto sep = line.find('\t');
    std::size_t sep_len = 1;
    if (sep == std::string::npos) {
        sep = line.find("\\t");
        if (sep != std::string::npos) sep_len = 2;
    }
    if (sep == std::string::npos) {
        std::cerr << "[music] unparsable yt-dlp line: " << line << std::endl;
        return std::nullopt;
    }
    MusicTrack t;
    t.title = line.substr(0, sep);
    t.url   = line.substr(sep + sep_len);
    if (t.url.empty()) return std::nullopt;
    return t;
}

} // namespace hecquin::music::yt
