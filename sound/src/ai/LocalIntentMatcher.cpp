#include "ai/LocalIntentMatcher.hpp"

#include "actions/DeviceAction.hpp"
#include "actions/GrammarCorrectionAction.hpp"
#include "actions/MusicAction.hpp"
#include "actions/PronunciationFeedbackAction.hpp"
#include "actions/TopicSearchAction.hpp"
#include "common/StringUtils.hpp"

#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace hecquin::ai {

using hecquin::common::to_lower_copy;
using hecquin::common::trim_copy;

namespace {

std::vector<std::string> split_phrases(const std::string& list) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : list) {
        if (c == '|' || c == ',' || c == ';' || c == '\n') {
            const std::string t = trim_copy(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    const std::string t = trim_copy(cur);
    if (!t.empty()) out.push_back(t);
    return out;
}

std::string escape_for_regex(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (char c : s) {
        switch (c) {
            case '.': case '^': case '$': case '|': case '(': case ')':
            case '[': case ']': case '{': case '}': case '*': case '+':
            case '?': case '\\':
                out.push_back('\\');
                [[fallthrough]];
            default:
                out.push_back(c);
        }
    }
    return out;
}

std::string alternation_from_list(const std::string& raw, const std::string& fallback) {
    const auto phrases = split_phrases(raw);
    if (phrases.empty()) return fallback;
    std::ostringstream oss;
    oss << "\\b(";
    for (std::size_t i = 0; i < phrases.size(); ++i) {
        if (i) oss << '|';
        oss << escape_for_regex(phrases[i]);
    }
    oss << ")\\b";
    return oss.str();
}

// libc++ `std::regex` is not guaranteed to be race-safe across threads; each
// matcher holds configured pattern *strings* and lazily compiles them into a
// `thread_local` cache keyed by the matcher address, giving us
// compile-once-per-thread safety without re-compiling per call.
struct Compiled {
    std::regex device;
    std::regex story;
    std::regex music;
    std::regex lesson_start;
    std::regex lesson_end;
    std::regex drill_start;
    std::regex drill_end;
};

Compiled compile(const LocalIntentMatcherConfig& cfg) {
    auto make = [](const std::string& pat) {
        return std::regex(pat,
            std::regex_constants::ECMAScript | std::regex_constants::icase);
    };
    return Compiled{
        make(cfg.device_pattern),
        make(cfg.story_pattern),
        make(cfg.music_pattern),
        make(cfg.lesson_start_pattern),
        make(cfg.lesson_end_pattern),
        make(cfg.drill_start_pattern),
        make(cfg.drill_end_pattern),
    };
}

const Compiled& cached_compile(const LocalIntentMatcherConfig& cfg) {
    // thread_local by key (address) so updating the matcher's config does not
    // require rebuilding.  In practice there is exactly one matcher per
    // binary, so the cache stays tiny.
    thread_local const LocalIntentMatcherConfig* last_cfg = nullptr;
    thread_local Compiled compiled{};
    if (last_cfg != &cfg) {
        compiled = compile(cfg);
        last_cfg = &cfg;
    }
    return compiled;
}

} // namespace

LocalIntentMatcherConfig LocalIntentMatcherConfig::from_phrase_lists(
    const std::string& lesson_start,
    const std::string& lesson_end,
    const std::string& drill_start,
    const std::string& drill_end) {
    LocalIntentMatcherConfig out;
    out.lesson_start_pattern =
        alternation_from_list(lesson_start, out.lesson_start_pattern);
    out.lesson_end_pattern =
        alternation_from_list(lesson_end, out.lesson_end_pattern);
    out.drill_start_pattern =
        alternation_from_list(drill_start, out.drill_start_pattern);
    out.drill_end_pattern =
        alternation_from_list(drill_end, out.drill_end_pattern);
    return out;
}

LocalIntentMatcher::LocalIntentMatcher(LocalIntentMatcherConfig cfg)
    : cfg_(std::move(cfg)) {}

std::optional<Action> LocalIntentMatcher::match(const std::string& transcript) const {
    const std::string trimmed = trim_copy(transcript);
    const std::string normalized = to_lower_copy(trimmed);
    if (normalized.empty()) return std::nullopt;

    const Compiled& p = cached_compile(cfg_);

    if (std::regex_search(normalized, p.lesson_start)) {
        LessonModeToggleAction a;
        a.enable = true;
        a.reply = "English lesson mode on. Say a sentence and I will help.";
        return a.into_action(trimmed);
    }

    if (std::regex_search(normalized, p.lesson_end)) {
        LessonModeToggleAction a;
        a.enable = false;
        a.reply = "Lesson mode off.";
        return a.into_action(trimmed);
    }

    if (std::regex_search(normalized, p.drill_start)) {
        DrillModeToggleAction a;
        a.enable = true;
        a.reply = "Pronunciation drill on. Repeat after me.";
        return a.into_action(trimmed);
    }

    if (std::regex_search(normalized, p.drill_end)) {
        DrillModeToggleAction a;
        a.enable = false;
        a.reply = "Drill mode off.";
        return a.into_action(trimmed);
    }

    std::smatch m;
    if (std::regex_search(normalized, m, p.device)) {
        const std::string verb   = to_lower_copy(trim_copy(std::string(m[1].first, m[1].second)));
        const std::string target = to_lower_copy(trim_copy(std::string(m[2].first, m[2].second)));
        const DevicePowerVerb power = (verb == "turn on")
            ? DevicePowerVerb::TurnOn : DevicePowerVerb::TurnOff;
        const DeviceOption device = (target == "air")
            ? DeviceOption::AirConditioning : DeviceOption::Switch;
        return DeviceAction{power, device}.into_action(trimmed);
    }

    if (std::regex_search(normalized, p.story)) {
        return TopicSearchAction{}.into_action(trimmed);
    }

    if (std::regex_search(normalized, p.music)) {
        return MusicAction{}.into_action(trimmed);
    }

    return std::nullopt;
}

} // namespace hecquin::ai
