#include "learning/ingest/ContentFingerprint.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using hecquin::learning::ingest::content_fingerprint;

namespace {

int failures = 0;

void expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "[FAIL] " << label << std::endl;
        ++failures;
    }
}

} // namespace

int main() {
    // Determinism: same content -> same fingerprint.
    const std::string a = "hello world";
    expect(content_fingerprint(a) == content_fingerprint(a),
           "same content produces same fingerprint");

    // Whitespace sensitivity: a leading space changes the fingerprint.
    expect(content_fingerprint("hello world") != content_fingerprint(" hello world"),
           "leading whitespace shifts fingerprint");

    // Length encoded in prefix: two inputs of different sizes cannot collide.
    const std::string f1 = content_fingerprint("abc");
    const std::string f2 = content_fingerprint("abcd");
    expect(f1 != f2, "different lengths produce different fingerprints");

    // Empty input is well-defined (FNV basis + 0 length).
    const std::string empty = content_fingerprint("");
    expect(!empty.empty(), "empty input still produces a non-empty fingerprint");
    expect(content_fingerprint("") == empty, "empty-input determinism");

    // Byte-level sensitivity.
    expect(content_fingerprint("AB") != content_fingerprint("BA"),
           "byte order affects fingerprint");

    if (failures == 0) {
        std::cout << "[test_content_fingerprint] all assertions passed" << std::endl;
        return 0;
    }
    return 1;
}
