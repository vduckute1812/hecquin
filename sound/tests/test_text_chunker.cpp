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
    using hecquin::learning::chunk_lines;

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
    {
        // Regression: chunker must never split inside a UTF-8 multi-byte
        // sequence. nlohmann/json aborts with type_error 316 ("invalid UTF-8
        // byte at index 0") when a chunk begins with an orphan continuation
        // byte like 0xA0 (the tail of U+00A0 NON-BREAKING SPACE = 0xC2 0xA0).
        //
        // Build a long run of U+00A0 pairs so that naive byte-offset splits
        // (with any step size) are virtually guaranteed to land mid-codepoint
        // unless the chunker is UTF-8-aware.
        std::string utf8;
        for (int i = 0; i < 4000; ++i) utf8 += "\xC2\xA0";  // U+00A0
        const auto c = chunk_text(utf8, 1800, 200);
        if (c.empty()) return fail("utf8 chunker produced no chunks");
        for (const auto& piece : c) {
            // Every chunk must be valid UTF-8: it may not start or end with a
            // continuation byte (10xxxxxx).
            if (piece.empty()) continue;
            const unsigned char first = static_cast<unsigned char>(piece.front());
            const unsigned char last  = static_cast<unsigned char>(piece.back());
            if ((first & 0xC0u) == 0x80u) {
                return fail("utf8 chunk begins with an orphan continuation byte");
            }
            // For a 2-byte lead (110xxxxx) the next byte must be a continuation;
            // we guarantee this by disallowing a chunk that ends on a bare
            // lead byte. Same for 3- and 4-byte leads.
            if ((last & 0xE0u) == 0xC0u ||  // 110xxxxx
                (last & 0xF0u) == 0xE0u ||  // 1110xxxx
                (last & 0xF8u) == 0xF0u) {  // 11110xxx
                return fail("utf8 chunk ends on a bare lead byte (sequence truncated)");
            }
        }
    }
    // ---- chunk_lines ---------------------------------------------------------
    {
        const auto c = chunk_lines("", 100);
        if (!c.empty()) return fail("chunk_lines empty input");
    }
    {
        // Three short JSONL records well under budget should pack into one chunk.
        const std::string a = R"({"word":"autumnal","meanings":["of autumn"]})";
        const std::string b = R"({"word":"amazing","meanings":["inspiring awe"]})";
        const std::string d = R"({"word":"awesome","meanings":["inspiring awe"]})";
        const std::string text = a + "\n" + b + "\n" + d + "\n";
        const auto c = chunk_lines(text, 500);
        if (c.size() != 1) return fail("chunk_lines packs three short lines into one chunk");
        if (c[0] != a + "\n" + b + "\n" + d) {
            return fail("chunk_lines preserves line order and joins with newline");
        }
    }
    {
        // Every line in every chunk must be a full JSONL record (start with '{'
        // and end with '}') — i.e. no line was ever split.
        std::string text;
        for (int i = 0; i < 40; ++i) {
            text += R"({"word":"w)" + std::to_string(i) + R"(","meanings":["m)" +
                    std::to_string(i) + R"("]})" + "\n";
        }
        const auto c = chunk_lines(text, 120);
        if (c.empty()) return fail("chunk_lines produced no chunks for JSONL corpus");
        for (const auto& piece : c) {
            size_t start = 0;
            while (start <= piece.size()) {
                const size_t nl = piece.find('\n', start);
                const size_t stop = (nl == std::string::npos) ? piece.size() : nl;
                if (stop == start) break;
                if (piece[start] != '{' || piece[stop - 1] != '}') {
                    return fail("chunk_lines: line in chunk is not a full {...} record");
                }
                if (nl == std::string::npos) break;
                start = nl + 1;
            }
        }
    }
    {
        // A single line longer than the budget becomes its own oversize chunk;
        // it is not truncated.
        const std::string huge(500, 'x');
        const auto c = chunk_lines(huge + "\n" + "small", 100);
        if (c.size() != 2) return fail("chunk_lines oversize line emits its own chunk");
        if (c[0].size() != 500) return fail("chunk_lines oversize line must not be truncated");
        if (c[1] != "small") return fail("chunk_lines continues after oversize line");
    }
    {
        // CRLF input: trailing '\r' is stripped from each line.
        const auto c = chunk_lines("alpha\r\nbeta\r\n", 100);
        if (c.size() != 1 || c[0] != "alpha\nbeta") return fail("chunk_lines CRLF handling");
    }
    {
        // Blank lines are dropped, not treated as content.
        const auto c = chunk_lines("one\n\n\ntwo\n", 100);
        if (c.size() != 1 || c[0] != "one\ntwo") return fail("chunk_lines drops blank lines");
    }
    return 0;
}
