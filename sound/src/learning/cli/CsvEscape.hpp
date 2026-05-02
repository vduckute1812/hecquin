#pragma once

#include <string>
#include <string_view>

namespace hecquin::learning::cli {

/** RFC 4180-ish quoting: wrap fields with `,`, `"`, CR, or LF and double inner `"`. */
inline std::string csv_escape(std::string_view field) {
    const bool needs_quoting =
        field.find_first_of(",\"\n\r") != std::string_view::npos;
    if (!needs_quoting) return std::string(field);

    std::string out;
    out.reserve(field.size() + 2);
    out.push_back('"');
    for (char c : field) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

} // namespace hecquin::learning::cli
