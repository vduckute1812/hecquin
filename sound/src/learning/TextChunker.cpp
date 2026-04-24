#include "learning/TextChunker.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace hecquin::learning {

namespace {

// UTF-8 continuation bytes have the top two bits set to 10xxxxxx. A codepoint
// boundary is any byte whose top two bits are NOT 10 (i.e., ASCII or a lead
// byte). Splitting inside a multi-byte sequence produces bytes that are
// invalid UTF-8 on their own — downstream nlohmann/json aborts with
// type_error 316 when the embedding request is serialised.
inline bool is_utf8_continuation(unsigned char b) {
    return (b & 0xC0u) == 0x80u;
}

// Advance `pos` forward until it sits on a codepoint boundary (or EOF).
// Never moves beyond `text.size()`.
size_t align_up_to_codepoint(const std::string& text, size_t pos) {
    while (pos < text.size() &&
           is_utf8_continuation(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    return pos;
}

} // namespace

std::vector<std::string> chunk_text(const std::string& text, int chunk_chars, int overlap) {
    std::vector<std::string> out;
    if (text.empty()) return out;
    if (chunk_chars <= 0) chunk_chars = 1800;
    if (overlap < 0) overlap = 0;
    if (overlap >= chunk_chars) overlap = chunk_chars / 4;

    const size_t step = static_cast<size_t>(chunk_chars - overlap);
    size_t i = 0;
    while (i < text.size()) {
        size_t end = std::min(text.size(), i + static_cast<size_t>(chunk_chars));
        if (end < text.size()) {
            size_t soft = end;
            while (soft > i + static_cast<size_t>(chunk_chars / 2) &&
                   !std::isspace(static_cast<unsigned char>(text[soft]))) {
                --soft;
            }
            if (soft > i + static_cast<size_t>(chunk_chars / 2)) {
                end = soft;
            }
        }
        // Never cut inside a UTF-8 multi-byte sequence: if `end` landed on a
        // continuation byte, include the rest of the codepoint. Safe even at
        // EOF because align_up clamps to text.size().
        end = align_up_to_codepoint(text, end);

        std::string piece = text.substr(i, end - i);
        size_t a = 0, b = piece.size();
        while (a < b && std::isspace(static_cast<unsigned char>(piece[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(piece[b - 1]))) --b;
        if (b > a) out.emplace_back(piece.substr(a, b - a));
        if (end >= text.size()) break;

        // Advance by `step`, then realign to a codepoint boundary so the
        // next chunk doesn't begin with an orphan continuation byte.
        size_t next = align_up_to_codepoint(text, i + step);
        if (next <= i) next = end;  // guarantee forward progress
        i = next;
    }
    return out;
}

std::vector<std::string> chunk_lines(const std::string& text, int budget_chars) {
    std::vector<std::string> out;
    if (text.empty()) return out;
    if (budget_chars <= 0) budget_chars = 1800;
    const size_t budget = static_cast<size_t>(budget_chars);

    std::string buffer;
    buffer.reserve(budget);

    auto flush = [&]() {
        if (!buffer.empty()) {
            out.emplace_back(std::move(buffer));
            buffer.clear();
        }
    };

    size_t i = 0;
    while (i < text.size()) {
        const size_t nl = text.find('\n', i);
        const size_t end = (nl == std::string::npos) ? text.size() : nl;
        size_t line_end = end;
        if (line_end > i && text[line_end - 1] == '\r') --line_end;

        // Skip blank (empty-after-trim) lines.
        if (line_end > i) {
            const std::string_view line(text.data() + i, line_end - i);

            // If the line alone exceeds budget, flush current buffer and emit
            // the oversize line as its own chunk (we never split a record).
            if (line.size() > budget) {
                flush();
                out.emplace_back(line);
            } else if (buffer.empty()) {
                buffer.assign(line);
            } else if (buffer.size() + 1 + line.size() <= budget) {
                buffer.push_back('\n');
                buffer.append(line);
            } else {
                flush();
                buffer.assign(line);
            }
        }

        if (nl == std::string::npos) break;
        i = nl + 1;
    }
    flush();
    return out;
}

} // namespace hecquin::learning
