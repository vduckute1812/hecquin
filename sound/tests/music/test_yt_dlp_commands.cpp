// Unit tests for music::yt::build_search_command and
// build_playback_command — pure command-string assembly.

#include "music/YouTubeMusicProvider.hpp"
#include "music/yt/YtDlpCommands.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void fail(const char* label, const std::string& cmd, const std::string& want) {
    std::cerr << "FAIL " << label << "\n  cmd:  " << cmd
              << "\n  want substring: " << want << std::endl;
    std::exit(1);
}

void test_search_command_includes_query_and_print_format() {
    hecquin::music::YouTubeMusicConfig cfg;
    cfg.yt_dlp_binary = "yt-dlp";
    const std::string cmd =
        hecquin::music::yt::build_search_command(cfg, "Daft Punk");

    if (!contains(cmd, "'yt-dlp'"))             fail("yt-dlp_quoted", cmd, "'yt-dlp'");
    if (!contains(cmd, "--no-warnings"))         fail("no-warnings", cmd, "--no-warnings");
    if (!contains(cmd, "--no-playlist"))         fail("no-playlist", cmd, "--no-playlist");
    if (!contains(cmd, "--default-search ytsearch"))
        fail("default-search", cmd, "--default-search ytsearch");
    if (!contains(cmd, "ytsearch1:Daft Punk music"))
        fail("query_wrapped", cmd, "ytsearch1:Daft Punk music");
    // The --print template MUST contain a real TAB (0x09) byte, not the
    // escape sequence — that's the long-standing yt-dlp quirk.
    if (!contains(cmd, std::string("'%(title)s") + '\t' + "%(webpage_url)s'"))
        fail("real_tab_in_print", cmd, "real TAB inside --print template");
}

void test_search_command_omits_cookies_when_path_missing() {
    hecquin::music::YouTubeMusicConfig cfg;
    cfg.cookies_file = "/this/path/should/never/exist/c00k13s.txt";
    const std::string cmd =
        hecquin::music::yt::build_search_command(cfg, "anything");
    if (contains(cmd, "--cookies"))
        fail("cookies_should_be_absent", cmd, "(no --cookies)");
}

void test_search_command_quotes_query_with_metacharacters() {
    hecquin::music::YouTubeMusicConfig cfg;
    const std::string cmd =
        hecquin::music::yt::build_search_command(cfg, "$(rm -rf /);");
    // The metachar payload must show up entirely *inside* a single-
    // quoted region — never escape into the shell's command space.
    if (!contains(cmd, "'ytsearch1:$(rm -rf /); music'"))
        fail("query_neutralised", cmd, "single-quoted ytsearch payload");
}

void test_playback_command_pipes_yt_dlp_into_ffmpeg() {
    hecquin::music::YouTubeMusicConfig cfg;
    cfg.yt_dlp_binary = "yt-dlp";
    cfg.ffmpeg_binary = "ffmpeg";
    cfg.sample_rate_hz = 48000;
    const std::string cmd = hecquin::music::yt::build_playback_command(
        cfg, "https://example.com/v=abc");

    if (!contains(cmd, "-f bestaudio -o -"))
        fail("yt_bestaudio", cmd, "-f bestaudio -o -");
    if (!contains(cmd, " | 'ffmpeg'"))
        fail("pipe_to_ffmpeg", cmd, " | 'ffmpeg'");
    if (!contains(cmd, "-f s16le -ac 1 -ar 48000"))
        fail("ffmpeg_format", cmd, "-f s16le -ac 1 -ar 48000");
    if (!contains(cmd, "'https://example.com/v=abc'"))
        fail("url_quoted", cmd, "'https://example.com/v=abc'");
}

} // namespace

int main() {
    test_search_command_includes_query_and_print_format();
    test_search_command_omits_cookies_when_path_missing();
    test_search_command_quotes_query_with_metacharacters();
    test_playback_command_pipes_yt_dlp_into_ffmpeg();
    std::cout << "[ok] test_yt_dlp_commands" << std::endl;
    return 0;
}
