#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace hecquin::common {

/**
 * Resolve `p` against `base_dir`. Empty `p` is returned untouched; absolute
 * `p` is normalised but not joined; otherwise returns `(base_dir / p)`
 * lexically-normalised.
 */
inline std::string resolve_against_dir(std::string_view base_dir, std::string p) {
    namespace fs = std::filesystem;
    if (p.empty()) return p;
    fs::path candidate(p);
    if (candidate.is_absolute()) {
        return candidate.lexically_normal().string();
    }
    if (base_dir.empty()) {
        return candidate.lexically_normal().string();
    }
    fs::path joined = fs::path(std::string(base_dir)) / candidate;
    return joined.lexically_normal().string();
}

} // namespace hecquin::common
