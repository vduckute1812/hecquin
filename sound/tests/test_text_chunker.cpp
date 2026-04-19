#include "learning/TextChunker.hpp"

#include <iostream>
#include <string>

namespace {

int fail(const char* message) {
    std::cerr << "[test_text_chunker] FAIL: " << message << std::endl;
    return 1;
}

} // namespace

int main() {
    using hecquin::learning::chunk_text;

    {
        const auto c = chunk_text("", 100, 10);
        if (!c.empty()) return fail("empty input");
    }
    {
        // Single short string — one chunk, no trimming issue.
        const auto c = chunk_text("hello world", 100, 10);
        if (c.size() != 1 || c[0] != "hello world") return fail("tiny input one chunk");
    }
    {
        // Long input without any whitespace should still chunk at fixed size.
        const std::string s(2000, 'a');
        const auto c = chunk_text(s, 500, 0);
        if (c.size() != 4) return fail("no-space hard split into 4");
        for (const auto& piece : c) {
            if (piece.size() > 500) return fail("chunk over limit");
        }
    }
    {
        // With whitespace, the splitter should prefer to break on a space so
        // no chunk cuts a word mid-letter.
        std::string s;
        for (int i = 0; i < 50; ++i) s += "word ";
        const auto c = chunk_text(s, 60, 10);
        if (c.empty()) return fail("chunked output non-empty");
        for (const auto& piece : c) {
            // Every chunk must start and end on a full 'word' token.
            if (piece.front() == ' ' || piece.back() == ' ') {
                return fail("chunk has leading/trailing whitespace");
            }
        }
    }
    {
        // Overlap keeps bridging context.
        const std::string a(100, 'a');
        const std::string b(100, 'b');
        const auto c = chunk_text(a + b, 120, 40);
        if (c.size() < 2) return fail("overlap must produce >= 2 chunks");
    }
    {
        // Overlap >= chunk_chars should be clamped.  Should still terminate.
        const auto c = chunk_text("abcdefghij", 4, 9999);
        if (c.empty()) return fail("pathological overlap must still terminate");
    }
    return 0;
}
