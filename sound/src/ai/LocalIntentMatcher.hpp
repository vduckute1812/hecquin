#pragma once

#include "actions/Action.hpp"

#include <optional>
#include <string>

struct LearningConfig;

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
    /**
     * "cancel music" / "stop music" / "exit music" — both the pre-play
     * bail-out (before the user has supplied a song name) and the
     * mid-song abort path.  Kept distinct from the lesson / drill exits
     * so phrasings don't leak across modes.
     */
    std::string music_cancel_pattern =
        R"(\b(cancel|stop|exit|close|end)\s+music\b)";
    /**
     * "pause music" — best-effort suspend of the current song.  The
     * provider may or may not honour it (SDL devices can; some
     * external streamers can't).
     */
    std::string music_pause_pattern = R"(\bpause\s+music\b)";
    /**
     * "continue music" / "resume music" / "play music" (the latter is
     * intentionally narrow: only matches when there is no other word
     * after `music`, so "play despacito" still goes through as a
     * regular query in `Music` mode).
     */
    std::string music_resume_pattern =
        R"(\b(continue|resume|unpause)\s+music\b)";
    std::string lesson_start_pattern =
        R"(\b(start|begin|open)\s+(english\s+)?lesson\b)";
    std::string lesson_end_pattern =
        R"(\b(exit|end|stop|quit)\s+(english\s+)?lesson\b)";
    std::string drill_start_pattern =
        R"(\b(start|begin|open)\s+(pronunciation|drill)\b)";
    std::string drill_end_pattern =
        R"(\b(exit|end|stop|quit)\s+(pronunciation\s+)?drill\b)";
    /**
     * "next / again / skip / continue" — drill-mode pacing intent used
     * when `HECQUIN_DRILL_AUTO_ADVANCE=0` so the learner advances at
     * their own pace.  Anchored on whole-utterance to avoid catching
     * "skip music" or "let me hear that again" inside a longer reply.
     */
    std::string drill_advance_pattern =
        R"(^\s*(next|again|skip|continue|move\s+on|keep\s+going)\s*\.?\s*$)";
    /**
     * Universal abort: "stop / cancel / never mind / forget it / shut
     * up".  Fires before music_cancel_pattern so a generic "stop" cuts
     * the assistant off mid-reply instead of being swallowed by the
     * music-cancel branch (which only matches "stop music").
     */
    std::string abort_pattern =
        R"(^\s*(never\s*mind|forget\s+it|shut\s*up|cancel|abort|stop\s*talking|stop)\s*\.?\s*$)";
    /** "help / what can I do / commands / capabilities". */
    std::string help_pattern =
        R"(\b(help|what\s+can\s+(i|you)\s+do|what\s+do\s+you\s+do|commands|capabilities)\b)";
    /** Music volume up — independent of an explicit "music" suffix so
     *  the user can say "louder" mid-song without re-invoking it. */
    std::string music_volume_up_pattern =
        R"(\b(louder|volume\s+up|turn\s+(it|music)\s+up|raise\s+(the\s+)?volume)\b)";
    std::string music_volume_down_pattern =
        R"(\b(quieter|softer|volume\s+down|turn\s+(it|music)\s+down|lower\s+(the\s+)?volume)\b)";
    std::string music_skip_pattern =
        R"(\b(skip|next\s+(song|track)|skip\s+(this|that))\b)";
    /** "go to sleep / mute yourself / stop listening". */
    std::string sleep_pattern =
        R"(\b(go\s+to\s+sleep|mute\s+yourself|stop\s+listening|sleep\s+mode)\b)";
    /** Wake phrase — runs even while in `ListenerMode::Asleep` so the
     *  user can address the assistant again.  Bare "wake up" is enough;
     *  "hello hecquin" / "hi hecquin" are extra-friendly. */
    std::string wake_pattern =
        R"(\b(wake\s+up|hello\s+hecquin|hi\s+hecquin|hey\s+hecquin)\b)";
    /** Identify the current speaker.  Captures the trailing name in
     *  group 1 so callers can populate a user row.  Lower-cases the
     *  name on extraction. */
    std::string identify_user_pattern =
        R"(\b(?:i'?m|this\s+is|my\s+name\s+is|call\s+me)\s+([a-z][a-z'\- ]{0,30}))";

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

    /**
     * Convenience: pull the four phrase lists straight off
     * `AppConfig::LearningConfig`.  Single source of truth so the
     * standalone `voice_detector` and the `LearningApp::matcher_config()`
     * helper do not drift apart.
     */
    static LocalIntentMatcherConfig make_from_learning(const LearningConfig& cfg);
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
