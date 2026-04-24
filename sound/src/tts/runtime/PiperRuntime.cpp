#include "tts/runtime/PiperRuntime.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef PIPER_EXECUTABLE
#define PIPER_EXECUTABLE "piper"
#endif

namespace hecquin::tts::runtime {

void configure() {
#ifdef __APPLE__
    std::string fallback_libs;
    const std::filesystem::path piper_path(PIPER_EXECUTABLE);

    if (piper_path.has_parent_path() && std::filesystem::exists(piper_path.parent_path())) {
        fallback_libs = piper_path.parent_path().string();
    }

    const auto prepend_if_exists = [&](const std::string& dir) {
        if (std::filesystem::exists(dir)) {
            fallback_libs += (fallback_libs.empty() ? "" : ":") + dir;
        }
    };
    prepend_if_exists("/opt/homebrew/opt/espeak-ng/lib");
    prepend_if_exists("/opt/homebrew/lib");
    prepend_if_exists("/usr/local/lib");

    if (const char* current = std::getenv("DYLD_FALLBACK_LIBRARY_PATH")) {
        if (*current != '\0') {
            fallback_libs += (fallback_libs.empty() ? "" : ":") + std::string(current);
        }
    }

    if (!fallback_libs.empty()) {
        setenv("DYLD_FALLBACK_LIBRARY_PATH", fallback_libs.c_str(), 1);
        setenv("DYLD_LIBRARY_PATH", fallback_libs.c_str(), 1);
    }
#endif
}

} // namespace hecquin::tts::runtime
