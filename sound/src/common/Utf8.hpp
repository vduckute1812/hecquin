#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

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

namespace utf8 {

/**
 * Length (in bytes) of the UTF-8 codepoint that starts with
 * `first_byte`.  Returns 1..4 for valid lead bytes (or ASCII), and `0`
 * for a continuation byte (`10xxxxxx`).  Malformed lead bytes are
 * treated as single-byte to keep the caller making forward progress.
 */
inline std::size_t codepoint_length(unsigned char first_byte) {
    if      ((first_byte & 0x80u) == 0x00u) return 1; // 0xxxxxxx ASCII
    else if ((first_byte & 0xC0u) == 0x80u) return 0; // continuation
    else if ((first_byte & 0xE0u) == 0xC0u) return 2;
    else if ((first_byte & 0xF0u) == 0xE0u) return 3;
    else if ((first_byte & 0xF8u) == 0xF0u) return 4;
    return 1;
}

/** True for UTF-8 continuation bytes (`10xxxxxx`). */
inline bool is_continuation(unsigned char b) {
    return (b & 0xC0u) == 0x80u;
}

/**
 * Advance `pos` forward over any continuation bytes so it lands on the
 * next codepoint boundary (or `s.size()`).  Never moves backwards;
 * always clamps to the end of the buffer.  Useful when slicing a
 * string at an arbitrary byte offset and you need to avoid splitting
 * a multi-byte character in half.
 */
inline std::size_t align_to_codepoint(std::string_view s, std::size_t pos) {
    while (pos < s.size() && is_continuation(static_cast<unsigned char>(s[pos]))) {
        ++pos;
    }
    return pos;
}

} // namespace utf8

} // namespace hecquin::common
