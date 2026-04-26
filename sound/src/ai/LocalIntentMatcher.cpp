#include "ai/LocalIntentMatcher.hpp"

#include "config/AppConfig.hpp"
#include "actions/DeviceAction.hpp"
#include "actions/GrammarCorrectionAction.hpp"
#include "actions/MusicAction.hpp"
#include "actions/PronunciationFeedbackAction.hpp"
#include "actions/SystemAction.hpp"
#include "actions/TopicSearchAction.hpp"
#include "common/StringUtils.hpp"

#include <array>
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
    std::regex music_cancel;
    std::regex music_pause;
    std::regex music_resume;
    std::regex music_volume_up;
    std::regex music_volume_down;
    std::regex music_skip;
    std::regex lesson_start;
    std::regex lesson_end;
    std::regex drill_start;
    std::regex drill_end;
    std::regex drill_advance;
    std::regex abort_intent;
    std::regex help_intent;
    std::regex sleep_intent;
    std::regex wake_intent;
    std::regex identify_user;
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
        make(cfg.music_cancel_pattern),
        make(cfg.music_pause_pattern),
        make(cfg.music_resume_pattern),
        make(cfg.music_volume_up_pattern),
        make(cfg.music_volume_down_pattern),
        make(cfg.music_skip_pattern),
        make(cfg.lesson_start_pattern),
        make(cfg.lesson_end_pattern),
        make(cfg.drill_start_pattern),
        make(cfg.drill_end_pattern),
        make(cfg.drill_advance_pattern),
        make(cfg.abort_pattern),
        make(cfg.help_pattern),
        make(cfg.sleep_pattern),
        make(cfg.wake_pattern),
        make(cfg.identify_user_pattern),
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

LocalIntentMatcherConfig
LocalIntentMatcherConfig::make_from_learning(const LearningConfig& cfg) {
    return from_phrase_lists(cfg.lesson_start_phrases,
                             cfg.lesson_end_phrases,
                             cfg.drill_start_phrases,
                             cfg.drill_end_phrases);
}

LocalIntentMatcher::LocalIntentMatcher(LocalIntentMatcherConfig cfg)
    : cfg_(std::move(cfg)) {}

namespace {

// Single-regex → fixed-action rule.  Table order = firing priority once
// the parametric branches (device verb/target + music cancel/pause/
// resume/play) have run; those keep bespoke handling outside the table.
struct SimpleRule {
    const std::regex Compiled::* pattern;
    Action (*build)(const std::string& trimmed);
};

const std::array<SimpleRule, 5> kSimpleRules = {{
    {&Compiled::lesson_start, [](const std::string& t) {
        LessonModeToggleAction a;
        a.enable = true;
        a.reply = "English lesson mode on. Say a sentence and I will help.";
        return a.into_action(t);
    }},
    {&Compiled::lesson_end, [](const std::string& t) {
        LessonModeToggleAction a;
        a.enable = false;
        a.reply = "Lesson mode off.";
        return a.into_action(t);
    }},
    {&Compiled::drill_start, [](const std::string& t) {
        DrillModeToggleAction a;
        a.enable = true;
        a.reply = "Pronunciation drill on. Repeat after me.";
        return a.into_action(t);
    }},
    {&Compiled::drill_end, [](const std::string& t) {
        DrillModeToggleAction a;
        a.enable = false;
        a.reply = "Drill mode off.";
        return a.into_action(t);
    }},
    {&Compiled::story, [](const std::string& t) {
        return TopicSearchAction{}.into_action(t);
    }},
}};

} // namespace

namespace {

// System intents that take the trimmed transcript only.  Abort / sleep /
// wake never carry mode-aware payloads at the matcher layer — the
// listener can amend the reply when applying side effects.  Help is
// handled separately so the listener can swap in the per-mode reply.
struct SystemRule {
    const std::regex Compiled::* pattern;
    Action (*build)(std::string trimmed);
};

const std::array<SystemRule, 3> kSystemRules = {{
    {&Compiled::abort_intent, &SystemAction::abort},
    {&Compiled::sleep_intent, &SystemAction::sleep},
    {&Compiled::wake_intent,  &SystemAction::wake},
}};

const char* kDefaultHelpReply =
    "Try things like: open music, start lesson, start drill, "
    "tell me a story, turn on the air, or just ask me a question. "
    "Say stop to interrupt me.";

} // namespace

std::optional<Action> LocalIntentMatcher::match(const std::string& transcript) const {
    const std::string trimmed = trim_copy(transcript);
    const std::string normalized = to_lower_copy(trimmed);
    if (normalized.empty()) return std::nullopt;

    const Compiled& p = cached_compile(cfg_);

    // System intents run first so a generic "stop" / "go to sleep" /
    // "wake up" cannot be shadowed by lesson / drill toggles.  Their
    // regexes are tightly anchored so they never collide with phrases
    // like "stop music" or "stop lesson".
    for (const auto& rule : kSystemRules) {
        if (std::regex_search(normalized, p.*rule.pattern)) {
            return rule.build(trimmed);
        }
    }

    // Drill-advance pacing intent.  Anchored to the whole utterance so
    // bare "next" / "skip" / "again" / "continue" only fires when the
    // user really meant it; embedded in a longer command (e.g. "skip
    // this song") goes elsewhere.  Mode-gating happens in the listener
    // — emitting the action unconditionally keeps the matcher simple.
    if (std::regex_search(normalized, p.drill_advance)) {
        Action a;
        a.kind = ActionKind::DrillAdvance;
        a.transcript = trimmed;
        return a;
    }

    // Help — emits a default reply; the listener swaps in a mode-aware
    // template before TTS speaks it.
    if (std::regex_search(normalized, p.help_intent)) {
        return SystemAction::help(kDefaultHelpReply, trimmed);
    }

    // "I'm Liam" / "this is Mia" — user identification.  Captured group
    // is the trailing name (lower-cased + trimmed); empty group falls
    // through to the generic "Got it." reply.
    {
        std::smatch m;
        if (std::regex_search(normalized, m, p.identify_user)) {
            std::string name;
            if (m.size() > 1) {
                name = trim_copy(std::string(m[1].first, m[1].second));
            }
            return SystemAction::identify(std::move(name), trimmed);
        }
    }

    // Lesson + drill toggles fire before any parametric branch; their
    // patterns are highly specific and cannot be confused with the
    // device or music regexes.  Run those first via the table.
    for (std::size_t i = 0; i < 4; ++i) {  // lesson_start/end + drill_start/end
        const auto& rule = kSimpleRules[i];
        if (std::regex_search(normalized, p.*rule.pattern)) {
            return rule.build(trimmed);
        }
    }

    // Device — needs the captured verb + target groups.
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

    // Story — single-regex fixed-action; runs *after* device because we
    // never want a "story" key word inside a device verb to swallow it.
    {
        const auto& rule = kSimpleRules[4];
        if (std::regex_search(normalized, p.*rule.pattern)) {
            return rule.build(trimmed);
        }
    }

    // Cancel / pause / resume / volume / skip must fire ahead of the
    // "open music" pattern so phrasings like "stop music" / "louder"
    // can't be mis-routed.  Each builds a different factory, so they
    // sit in a parallel array rather than the simple-action table.
    using MusicFactory = Action (*)(std::string);
    struct MusicRule { const std::regex Compiled::* pattern; MusicFactory build; };
    static const std::array<MusicRule, 7> kMusicRules = {{
        {&Compiled::music_cancel,      &MusicAction::cancel},
        {&Compiled::music_pause,       &MusicAction::pause},
        {&Compiled::music_resume,      &MusicAction::resume},
        {&Compiled::music_volume_up,   &MusicAction::volume_up},
        {&Compiled::music_volume_down, &MusicAction::volume_down},
        {&Compiled::music_skip,        &MusicAction::skip},
        {&Compiled::music,             &MusicAction::prompt},
    }};
    for (const auto& rule : kMusicRules) {
        if (std::regex_search(normalized, p.*rule.pattern)) {
            return rule.build(trimmed);
        }
    }

    return std::nullopt;
}

} // namespace hecquin::ai
