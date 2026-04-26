// Unit tests for hecquin::common::posix_sh_single_quote.
//
// The helper underwrites every shell-string command we hand to
// `/bin/sh -c` in `music/yt/*` and `tts/backend/PiperShellBackend.cpp`.
// A regression here means an injection vector or a parse failure for
// users whose model paths / queries contain `'`, spaces, tabs, or shell
// metacharacters.

#include "common/ShellEscape.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect_eq(const std::string& got, const std::string& want, const char* label) {
    if (got != want) {
        std::cerr << "FAIL " << label << "\n  got:  " << got
                  << "\n  want: " << want << std::endl;
        std::exit(1);
    }
}

void test_basic_word_is_wrapped_in_single_quotes() {
    expect_eq(hecquin::common::posix_sh_single_quote("hello"),
              "'hello'", "basic_word");
}

void test_empty_string_becomes_empty_quotes() {
    expect_eq(hecquin::common::posix_sh_single_quote(""), "''",
              "empty_string");
}

void test_whitespace_is_preserved_verbatim() {
    expect_eq(hecquin::common::posix_sh_single_quote("a b\tc"),
              "'a b\tc'", "whitespace");
}

void test_embedded_single_quote_is_escape_sequence() {
    // POSIX-safe single-quote escape: close, escape, reopen.
    expect_eq(hecquin::common::posix_sh_single_quote("it's"),
              "'it'\\''s'", "embedded_single_quote");
}

void test_metacharacters_are_neutralised() {
    // Anything other than `'` is opaque inside single quotes.  This is
    // the reason we picked single-quote escaping.
    expect_eq(
        hecquin::common::posix_sh_single_quote("$(rm -rf /); echo `id` & ;|"),
        "'$(rm -rf /); echo `id` & ;|'",
        "metachars");
}

void test_string_view_overload_compiles() {
    std::string_view sv = "abc";
    expect_eq(hecquin::common::posix_sh_single_quote(sv), "'abc'",
              "string_view");
}

} // namespace

int main() {
    test_basic_word_is_wrapped_in_single_quotes();
    test_empty_string_becomes_empty_quotes();
    test_whitespace_is_preserved_verbatim();
    test_embedded_single_quote_is_escape_sequence();
    test_metacharacters_are_neutralised();
    test_string_view_overload_compiles();
    std::cout << "[ok] test_shell_escape" << std::endl;
    return 0;
}
