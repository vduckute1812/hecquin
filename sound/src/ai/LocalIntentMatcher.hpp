#pragma once

#include "actions/Action.hpp"

#include <optional>
#include <string>

namespace hecquin::ai {

/**
 * Patterns used by the regex fast-path.  Each field is either a single
 * regex-safe pattern fragment *or* a pipe-separated alternation list.
 * Defaults mirror the historical hardcoded behaviour so existing callers
 * that pass `{}` keep working.
 *
 * `AppConfig::LearningConfig` exposes env-driven overrides; the executable
 * entry points build a matcher from that config so the matcher and the
 * listener's mode-toggle logic agree on what phrases mean what.
 */
struct LocalIntentMatcherConfig {
    std::string device_pattern =
        R"(^\s*(turn on|turn off)\s+(air|switch)\b)";
    std::string story_pattern = R"(\btell me a story\b)";
    std::string music_pattern = R"(\bopen music\b)";
    std::string lesson_start_pattern =
        R"(\b(start|begin|open)\s+(english\s+)?lesson\b)";
    std::string lesson_end_pattern =
        R"(\b(exit|end|stop|quit)\s+(english\s+)?lesson\b)";
    std::string drill_start_pattern =
        R"(\b(start|begin|open)\s+(pronunciation|drill)\b)";
    std::string drill_end_pattern =
        R"(\b(exit|end|stop|quit)\s+(pronunciation\s+)?drill\b)";

    /**
     * Build a matcher config from space/comma/pipe-separated phrase lists of
     * the kind `AppConfig::LearningConfig` exposes.  Phrases are escaped and
     * joined into a single alternation.  Unset (empty) lists fall back to the
     * hardcoded defaults, so partial configuration still works.
     */
    static LocalIntentMatcherConfig from_phrase_lists(
        const std::string& lesson_start,
        const std::string& lesson_end,
        const std::string& drill_start,
        const std::string& drill_end);
};

/**
 * Pure regex-based intent matcher for the fast-path (no network, no I/O).
 *
 * Handles a small set of wake-word style commands: turn on/off the air
 * conditioner, open music, tell a story, and enter/leave lesson / drill mode.
 * Trimming and lower-casing are done internally so callers can pass raw
 * transcripts.  Returns `nullopt` when nothing matches.
 *
 * Patterns are compiled once at construction; the matcher is stateless and
 * safe to share across threads only as `const` (internal `thread_local`
 * compilation cache avoids libc++'s cross-thread regex issues on older
 * toolchains).
 */
class LocalIntentMatcher {
public:
    LocalIntentMatcher() = default;
    explicit LocalIntentMatcher(LocalIntentMatcherConfig cfg);

    std::optional<Action> match(const std::string& transcript) const;

    const LocalIntentMatcherConfig& config() const { return cfg_; }

private:
    LocalIntentMatcherConfig cfg_;
};

} // namespace hecquin::ai
