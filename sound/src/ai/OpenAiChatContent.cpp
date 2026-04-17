#include "ai/OpenAiChatContent.hpp"

#include <cctype>
#include <cstdint>

namespace {

size_t skip_ws(const std::string& s, size_t i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
    return i;
}

bool read_hex_u16(const std::string& s, size_t i, std::uint16_t& out) {
    if (i + 4 > s.size()) {
        return false;
    }
    int v = 0;
    for (size_t k = 0; k < 4; ++k) {
        const unsigned char c = static_cast<unsigned char>(s[i + k]);
        int d = -1;
        if (c >= '0' && c <= '9') {
            d = static_cast<int>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = static_cast<int>(c - 'a') + 10;
        } else if (c >= 'A' && c <= 'F') {
            d = static_cast<int>(c - 'A') + 10;
        }
        if (d < 0) {
            return false;
        }
        v = v * 16 + d;
    }
    out = static_cast<std::uint16_t>(v);
    return true;
}

void push_utf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7FU) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FFU) {
        out += static_cast<char>(0xC0 | static_cast<char>(cp >> 6));
        out += static_cast<char>(0x80 | static_cast<char>(cp & 0x3FU));
    } else if (cp <= 0xFFFFU) {
        out += static_cast<char>(0xE0 | static_cast<char>(cp >> 12));
        out += static_cast<char>(0x80 | static_cast<char>((cp >> 6) & 0x3FU));
        out += static_cast<char>(0x80 | static_cast<char>(cp & 0x3FU));
    } else {
        out += static_cast<char>(0xF0 | static_cast<char>(cp >> 18));
        out += static_cast<char>(0x80 | static_cast<char>((cp >> 12) & 0x3FU));
        out += static_cast<char>(0x80 | static_cast<char>((cp >> 6) & 0x3FU));
        out += static_cast<char>(0x80 | static_cast<char>(cp & 0x3FU));
    }
}

/** `i` is the first character inside the JSON string (after the opening `"`). */
std::optional<std::string> read_json_string(const std::string& s, size_t& i) {
    std::string out;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '"') {
            ++i;
            return out;
        }
        if (c != '\\' || i + 1 >= s.size()) {
            out += c;
            ++i;
            continue;
        }
        const char e = s[i + 1];
        switch (e) {
            case '"':
                out += '"';
                i += 2;
                break;
            case '\\':
                out += '\\';
                i += 2;
                break;
            case '/':
                out += '/';
                i += 2;
                break;
            case 'b':
                out += '\b';
                i += 2;
                break;
            case 'f':
                out += '\f';
                i += 2;
                break;
            case 'n':
                out += '\n';
                i += 2;
                break;
            case 'r':
                out += '\r';
                i += 2;
                break;
            case 't':
                out += '\t';
                i += 2;
                break;
            case 'u': {
                std::uint16_t u;
                if (!read_hex_u16(s, i + 2, u)) {
                    return std::nullopt;
                }
                push_utf8(out, u);
                i += 6;
                break;
            }
            default:
                out += e;
                i += 2;
                break;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> extract_openai_chat_assistant_content(const std::string& json) {
    static constexpr char k_key[] = "\"content\"";
    static constexpr size_t k_key_len = sizeof(k_key) - 1;

    size_t scan = 0;
    const size_t choices = json.find("\"choices\"");
    if (choices != std::string::npos) {
        scan = choices;
    }
    const size_t message = json.find("\"message\"", scan);
    if (message != std::string::npos) {
        scan = message;
    }

    const size_t n = json.size();
    for (size_t pos = scan; pos < n;) {
        pos = json.find(k_key, pos);
        if (pos == std::string::npos) {
            return std::nullopt;
        }

        size_t i = pos + k_key_len;
        i = skip_ws(json, i);
        if (i >= n || json[i] != ':') {
            ++pos;
            continue;
        }
        ++i;
        i = skip_ws(json, i);
        if (i >= n) {
            return std::nullopt;
        }

        if (i + 4 <= n && json.compare(i, 4, "null") == 0) {
            return std::string();
        }
        if (json[i] != '"') {
            ++pos;
            continue;
        }
        ++i;
        return read_json_string(json, i);
    }
    return std::nullopt;
}
