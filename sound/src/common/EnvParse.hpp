#pragma once

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

/**
 * Header-only readers for `getenv`-driven configuration overrides.
 *
 * Every caller across the codebase used to roll its own
 * `parse_float_env` / `parse_int_env` etc.; collecting the helpers
 * here gives every reader the same warning format
 * (`[env] ignoring invalid <NAME>=<raw>`) and a single place to extend
 * when a new override type is needed.
 *
 * Contract for every parse_* function:
 *   - returns false (and leaves `out` untouched) if the variable is
 *     unset, empty, or fails to parse;
 *   - returns true and writes to `out` only on a clean parse.
 *
 * No heap allocation; underlying `std::sto*` exceptions are caught and
 * surfaced as a single warning line.
 */

namespace hecquin::common::env {

namespace detail {

inline void warn_invalid(const char* name, const char* raw) {
    std::cerr << "[env] ignoring invalid " << name << "=" << raw << std::endl;
}

inline const char* raw_or_null(const char* name) {
    const char* raw = std::getenv(name);
    if (!raw || *raw == '\0') return nullptr;
    return raw;
}

} // namespace detail

inline bool parse_float(const char* name, float& out) {
    const char* raw = detail::raw_or_null(name);
    if (!raw) return false;
    try {
        out = std::stof(raw);
        return true;
    } catch (...) {
        detail::warn_invalid(name, raw);
        return false;
    }
}

inline bool parse_int(const char* name, int& out) {
    const char* raw = detail::raw_or_null(name);
    if (!raw) return false;
    try {
        out = std::stoi(raw);
        return true;
    } catch (...) {
        detail::warn_invalid(name, raw);
        return false;
    }
}

inline bool parse_size(const char* name, std::size_t& out) {
    const char* raw = detail::raw_or_null(name);
    if (!raw) return false;
    try {
        const long long v = std::stoll(raw);
        if (v < 0) return false;
        out = static_cast<std::size_t>(v);
        return true;
    } catch (...) {
        detail::warn_invalid(name, raw);
        return false;
    }
}

inline bool parse_bool(const char* name, bool& out) {
    const char* raw = detail::raw_or_null(name);
    if (!raw) return false;
    const std::string s(raw);
    if (s == "0" || s == "false" || s == "off" || s == "no") {
        out = false;
        return true;
    }
    if (s == "1" || s == "true" || s == "on" || s == "yes") {
        out = true;
        return true;
    }
    detail::warn_invalid(name, raw);
    return false;
}

/** Returns the raw env value or `nullptr` when unset / empty. */
inline const char* read_string(const char* name) {
    return detail::raw_or_null(name);
}

} // namespace hecquin::common::env
