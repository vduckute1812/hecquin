#include "voice/CapabilityReport.hpp"

#include "common/EnvParse.hpp"
#include "config/AppConfig.hpp"

#include <sstream>
#include <sys/stat.h>

namespace hecquin::voice {

namespace {

bool file_exists(const std::string& path) {
    if (path.empty()) return false;
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

bool which_binary(const std::string& bin) {
    // Resolve `bin` via PATH only when it isn't already an absolute
    // path.  We don't need anything fancy — just "is the binary present
    // somewhere I can spawn it from?".
    if (bin.empty()) return false;
    if (bin.find('/') != std::string::npos) return file_exists(bin);
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string acc;
    for (const char* p = path; ; ++p) {
        if (*p == ':' || *p == '\0') {
            if (!acc.empty()) {
                std::string candidate = acc + "/" + bin;
                if (file_exists(candidate)) return true;
            }
            acc.clear();
            if (*p == '\0') break;
        } else {
            acc.push_back(*p);
        }
    }
    return false;
}

} // namespace

std::string CapabilityStatus::spoken_summary() const {
    if (quiet_boot) return {};
    std::ostringstream oss;
    bool first = true;
    auto add = [&](const char* sentence) {
        if (!first) oss << ' ';
        oss << sentence;
        first = false;
    };
    if (!cloud_ready)         add("Cloud assistant offline.");
    if (!pronunciation_ready) add("Pronunciation scoring unavailable.");
    if (!music_ready)         add("Music provider unavailable.");
    if (first) return {}; // everything is ready — no need to chatter
    add("Local commands still work.");
    return oss.str();
}

CapabilityStatus probe_capabilities(const AppConfig& cfg) {
    CapabilityStatus s;
    namespace env = hecquin::common::env;
    bool quiet = false;
    if (env::parse_bool("HECQUIN_QUIET_BOOT", quiet)) s.quiet_boot = quiet;

    s.cloud_ready = cfg.ai.ready();
    s.pronunciation_ready = file_exists(cfg.pronunciation.model_path);
    s.music_ready = which_binary(cfg.music.yt_dlp_binary) &&
                    which_binary(cfg.music.ffmpeg_binary);
    return s;
}

} // namespace hecquin::voice
