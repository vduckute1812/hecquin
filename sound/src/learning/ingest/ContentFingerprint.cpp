#include "learning/ingest/ContentFingerprint.hpp"

#include <cstdint>
#include <sstream>

namespace hecquin::learning::ingest {

std::string content_fingerprint(const std::string& content) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : content) {
        h ^= c;
        h *= 1099511628211ull;
    }
    std::ostringstream oss;
    oss << std::hex << content.size() << "-" << h;
    return oss.str();
}

} // namespace hecquin::learning::ingest
