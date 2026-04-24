#pragma once

#include <cstdint>
#include <string>

namespace hecquin::common {

/// Return a valid-UTF-8 copy of `s`.
///
/// Any byte(s) that do not form a well-formed UTF-8 sequence (per RFC 3629 —
/// no overlong encodings, no surrogate halves, no codepoints above U+10FFFF)
/// are replaced with `replacement`. Valid sequences are copied verbatim.
///
/// This is what we need before feeding strings into nlohmann::json, whose
/// `dump()` throws on invalid UTF-8 — and before storing user-supplied text
/// in SQLite, where downstream tools assume UTF-8.
///
/// Default replacement is a single ASCII space. That turns Windows-1252
/// artifacts like 0xA0 (NBSP between CSV cells) into a normal word break,
/// which tends to be what you actually want for vocabulary corpora. Use the
/// U+FFFD replacement character ("\xEF\xBF\xBD") if you need to keep the
/// position of the bad byte visible instead.
inline std::string sanitize_utf8(const std::string& s, char replacement = ' ') {
    std::string out;
    out.reserve(s.size());
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char b0 = static_cast<unsigned char>(s[i]);

        // 1-byte ASCII fast path.
        if (b0 < 0x80) {
            out.push_back(static_cast<char>(b0));
            ++i;
            continue;
        }

        // Figure out how many continuation bytes this leader claims + the
        // smallest codepoint that may legally be encoded at this length
        // (rejects overlong forms).
        int need = 0;
        std::uint32_t cp = 0;
        std::uint32_t cp_min = 0;
        if ((b0 & 0xE0) == 0xC0) { need = 1; cp = b0 & 0x1Fu; cp_min = 0x80; }
        else if ((b0 & 0xF0) == 0xE0) { need = 2; cp = b0 & 0x0Fu; cp_min = 0x800; }
        else if ((b0 & 0xF8) == 0xF0) { need = 3; cp = b0 & 0x07u; cp_min = 0x10000; }
        else {
            // Continuation byte in leader position, or illegal 5/6-byte form.
            out.push_back(replacement);
            ++i;
            continue;
        }

        if (i + static_cast<size_t>(need) + 1 > n) {
            // Truncated trailing sequence.
            out.push_back(replacement);
            ++i;
            continue;
        }

        bool ok = true;
        for (int k = 1; k <= need; ++k) {
            const unsigned char b = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
            if ((b & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (b & 0x3Fu);
        }
        if (ok) {
            if (cp < cp_min) ok = false;                       // overlong
            else if (cp >= 0xD800 && cp <= 0xDFFF) ok = false; // surrogate half
            else if (cp > 0x10FFFFu) ok = false;               // out of Unicode
        }
        if (!ok) {
            out.push_back(replacement);
            ++i;
            continue;
        }

        for (int k = 0; k <= need; ++k) out.push_back(s[i + static_cast<size_t>(k)]);
        i += static_cast<size_t>(need) + 1;
    }
    return out;
}

} // namespace hecquin::common
