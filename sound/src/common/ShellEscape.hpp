#pragma once

#include <string>
#include <string_view>

/**
 * Single-source POSIX `/bin/sh` quoting.
 *
 * Both `tts/backend/PiperShellBackend.cpp` and the music subsystem build
 * shell command lines passed to `std::system` / `execl /bin/sh -c`.  Both used
 * to roll their own `shell_quote` / `shell_escape` with the same single-
 * quote algorithm.  Centralising the helper here removes that
 * duplication and makes it the obvious place to add new escape variants
 * (double-quote, --argument= form, …) if a future caller needs them.
 *
 * Algorithm: wrap the value in single quotes, replacing every embedded
 * `'` with `'\''` (close quote, escaped quote, reopen).  Safe for any
 * payload — bytes are passed through verbatim by POSIX sh inside single
 * quotes, so tabs / newlines / embedded shell metacharacters are
 * neutralised.
 */
namespace hecquin::common {

inline std::string posix_sh_single_quote(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (char c : value) {
        if (c == '\'') {
            out.append("'\\''");
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

} // namespace hecquin::common
