#include "common/Utf8.hpp"

#include <cassert>
#include <iostream>
#include <string>

using hecquin::common::sanitize_utf8;

namespace {

void expect_eq(const std::string& got, const std::string& want, const char* label) {
    if (got != want) {
        std::cerr << "[FAIL] " << label
                  << "\n  got : " << got << " (" << got.size() << " bytes)"
                  << "\n  want: " << want << " (" << want.size() << " bytes)"
                  << std::endl;
        std::exit(1);
    }
}

} // namespace

int main() {
    // ASCII round-trips byte-for-byte.
    expect_eq(sanitize_utf8("hello world"), "hello world", "ASCII pass-through");

    // Valid multi-byte UTF-8 is preserved: "café" = 63 61 66 C3 A9.
    expect_eq(sanitize_utf8("caf\xC3\xA9"), "caf\xC3\xA9", "valid 2-byte UTF-8 preserved");

    // Valid 3-byte UTF-8: U+6587 (文) = E6 96 87.
    expect_eq(sanitize_utf8("\xE6\x96\x87"), "\xE6\x96\x87", "valid 3-byte UTF-8 preserved");

    // The crash reporter: lone 0xA0 (CP-1252 NBSP) between two ASCII tokens.
    // It should become a space so word boundaries survive, matching the
    // Ingestor's expectation for vocabulary CSVs.
    expect_eq(sanitize_utf8("hello\xA0world"), "hello world", "lone 0xA0 -> space");

    // Orphan continuation bytes.
    expect_eq(sanitize_utf8("a\x80\x80z"), "a  z", "orphan continuations replaced");

    // Leader with missing continuations at end of string.
    expect_eq(sanitize_utf8("x\xC3"), "x ", "truncated 2-byte sequence");

    // Overlong NUL (C0 80) — valid-looking bytes, invalid semantics.
    expect_eq(sanitize_utf8("a\xC0\x80" "b"), "a  b", "overlong NUL rejected");

    // Surrogate half (ED A0 80 = U+D800) — rejected per RFC 3629.
    expect_eq(sanitize_utf8("a\xED\xA0\x80" "b"), "a   b", "surrogate half rejected");

    // Mixed: ASCII + valid + invalid + valid.
    expect_eq(sanitize_utf8("A\xC3\xA9_\xA0_\xE6\x96\x87"),
              "A\xC3\xA9_ _\xE6\x96\x87",
              "mixed good/bad bytes");

    // Empty string stays empty.
    expect_eq(sanitize_utf8(""), "", "empty string");

    // The sanitized output must itself be valid UTF-8 — sanitize is idempotent.
    const std::string once = sanitize_utf8(
        "noise: \xA0\xFF\xED\xA0\x80 tail caf\xC3\xA9");
    expect_eq(sanitize_utf8(once), once, "sanitize is idempotent");

    // Explicit replacement char (U+FFFD leader byte wouldn't survive as ASCII
    // char, so we just use '?' here).
    expect_eq(sanitize_utf8("bad\xA0" "byte", '?'), "bad?byte",
              "custom replacement char");

    std::cout << "[OK] Utf8 sanitize: all cases passed\n";
    return 0;
}
