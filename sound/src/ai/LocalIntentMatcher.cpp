#include "ai/LocalIntentMatcher.hpp"

#include "actions/DeviceAction.hpp"
#include "actions/GrammarCorrectionAction.hpp"
#include "actions/MusicAction.hpp"
#include "actions/PronunciationFeedbackAction.hpp"
#include "actions/TopicSearchAction.hpp"
#include "common/StringUtils.hpp"

#include <regex>

namespace hecquin::ai {

using hecquin::common::to_lower_copy;
using hecquin::common::trim_copy;

namespace {

// libc++ `std::regex` is not guaranteed to be race-safe across threads; the
// matcher is stateless and cheap enough to rebuild per call, so we keep the
// regexes `thread_local` to combine "compile once per thread" with safety.
struct Patterns {
    std::regex device{R"(^\s*(turn on|turn off)\s+(air|switch)\b)",
                       std::regex_constants::ECMAScript | std::regex_constants::icase};
    std::regex story{R"(\btell me a story\b)",
                     std::regex_constants::ECMAScript | std::regex_constants::icase};
    std::regex music{R"(\bopen music\b)",
                     std::regex_constants::ECMAScript | std::regex_constants::icase};
    std::regex lesson_start{R"(\b(start|begin|open)\s+(english\s+)?lesson\b)",
                             std::regex_constants::ECMAScript | std::regex_constants::icase};
    std::regex lesson_end{R"(\b(exit|end|stop|quit)\s+(english\s+)?lesson\b)",
                           std::regex_constants::ECMAScript | std::regex_constants::icase};
    std::regex drill_start{R"(\b(start|begin|open)\s+(pronunciation|drill)\b)",
                            std::regex_constants::ECMAScript | std::regex_constants::icase};
    std::regex drill_end{R"(\b(exit|end|stop|quit)\s+(pronunciation\s+)?drill\b)",
                          std::regex_constants::ECMAScript | std::regex_constants::icase};
};

const Patterns& patterns() {
    thread_local Patterns p;
    return p;
}

} // namespace

std::optional<Action> LocalIntentMatcher::match(const std::string& transcript) const {
    const std::string trimmed = trim_copy(transcript);
    const std::string normalized = to_lower_copy(trimmed);
    if (normalized.empty()) return std::nullopt;

    const auto& p = patterns();

    if (std::regex_search(normalized, p.lesson_start)) {
        LessonModeToggleAction a;
        a.enable = true;
        a.reply = "English lesson mode on. Say a sentence and I will help.";
        auto out = a.into_action(trimmed);
        return out;
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
