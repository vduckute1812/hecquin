// Unit tests for music::yt::parse_search_output — covers the two
// long-standing yt-dlp quirks: progress-line preamble and the
// occasional literal "\\t" separator.

#include "music/yt/YtDlpSearch.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect_match(std::optional<hecquin::music::MusicTrack> got,
                  const std::string& want_title,
                  const std::string& want_url,
                  const char* label) {
    if (!got.has_value()) {
        std::cerr << "FAIL " << label << ": expected match, got nullopt"
                  << std::endl;
        std::exit(1);
    }
    if (got->title != want_title || got->url != want_url) {
        std::cerr << "FAIL " << label
                  << "\n  got title='" << got->title << "' url='" << got->url
                  << "'\n  want title='" << want_title << "' url='" << want_url
                  << "'" << std::endl;
        std::exit(1);
    }
}

void expect_none(std::optional<hecquin::music::MusicTrack> got,
                 const char* label) {
    if (got.has_value()) {
        std::cerr << "FAIL " << label
                  << ": expected nullopt, got title='" << got->title
                  << "' url='" << got->url << "'" << std::endl;
        std::exit(1);
    }
}

void test_real_tab_separator() {
    const std::string raw =
        "Daft Punk - Around the World\thttps://youtu.be/abc\n";
    expect_match(hecquin::music::yt::parse_search_output(raw),
                 "Daft Punk - Around the World",
                 "https://youtu.be/abc",
                 "real_tab");
}

void test_literal_backslash_t_fallback() {
    // Some yt-dlp builds historically printed the two-byte sequence
    // `\` `t` instead of a real TAB.
    const std::string raw = "Some Song\\thttps://youtu.be/xyz\n";
    expect_match(hecquin::music::yt::parse_search_output(raw),
                 "Some Song",
                 "https://youtu.be/xyz",
                 "backslash_t_fallback");
}

void test_skip_leading_blank_lines() {
    // Real-world stdout from yt-dlp is just the `--print` payload —
    // progress / warning text goes to stderr (deliberately not captured
    // by `Subprocess::spawn_read`).  The parser only needs to step past
    // accidental leading blank bytes, which can happen on slow links.
    const std::string raw = "\n\n  Hit Title\thttps://youtu.be/hit\n";
    expect_match(hecquin::music::yt::parse_search_output(raw),
                 "  Hit Title",
                 "https://youtu.be/hit",
                 "blank_preamble_skipped");
}

void test_empty_input_returns_nullopt() {
    expect_none(hecquin::music::yt::parse_search_output(""), "empty");
    expect_none(hecquin::music::yt::parse_search_output("\n\n\n"),
                "blank_lines");
}

void test_unparsable_line_returns_nullopt() {
    // No TAB anywhere → parse failure is logged and surfaced.
    expect_none(
        hecquin::music::yt::parse_search_output("just some text no separator\n"),
        "no_separator");
}

void test_empty_url_returns_nullopt() {
    // Title present but URL field is blank.
    expect_none(hecquin::music::yt::parse_search_output("Title\t\n"),
                "empty_url");
}

void test_no_trailing_newline_still_parses() {
    expect_match(hecquin::music::yt::parse_search_output("T\tU"),
                 "T", "U", "no_trailing_newline");
}

} // namespace

int main() {
    test_real_tab_separator();
    test_literal_backslash_t_fallback();
    test_skip_leading_blank_lines();
    test_empty_input_returns_nullopt();
    test_unparsable_line_returns_nullopt();
    test_empty_url_returns_nullopt();
    test_no_trailing_newline_still_parses();
    std::cout << "[ok] test_yt_dlp_search_parser" << std::endl;
    return 0;
}
