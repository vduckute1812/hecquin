#include "voice/WakeWordGate.hpp"

#include "common/EnvParse.hpp"

#include <algorithm>
#include <cctype>

namespace hecquin::voice {

namespace {

constexpr const char* kDefaultWakePattern =
    R"(^\s*(hey|hi|hello|ok)?\s*hecquin\s*[,\.\!\?]?\s*)";

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

WakeWordGate::Mode parse_mode(const std::string& raw) {
    const std::string s = to_lower(raw);
    if (s == "wake_word" || s == "wake-word" || s == "wake") return WakeWordGate::Mode::WakeWord;
    if (s == "ptt" || s == "push_to_talk")                   return WakeWordGate::Mode::Ptt;
    return WakeWordGate::Mode::Always;
}

} // namespace

WakeWordGate::WakeWordGate()
    : wake_re_(kDefaultWakePattern,
               std::regex_constants::ECMAScript | std::regex_constants::icase),
      wake_re_src_(kDefaultWakePattern) {}

void WakeWordGate::apply_env_overrides() {
    namespace env = hecquin::common::env;
    if (const char* m = env::read_string("HECQUIN_WAKE_MODE")) {
        mode_.store(parse_mode(m), std::memory_order_release);
    }
    int iv = 0;
    if (env::parse_int("HECQUIN_WAKE_WINDOW_MS", iv)) {
        wake_window_ms_ = std::max(0, iv);
    }
    if (const char* phrase = env::read_string("HECQUIN_WAKE_PHRASE")) {
        try {
            wake_re_ = std::regex(phrase,
                std::regex_constants::ECMAScript | std::regex_constants::icase);
            wake_re_src_ = phrase;
        } catch (...) {
            // Bad regex — keep the default and log.  We never throw out
            // of this call because that would prevent the listener from
            // booting on a malformed env.
        }
    }
}

void WakeWordGate::set_ptt_pressed(bool pressed) {
    ptt_pressed_.store(pressed, std::memory_order_release);
}

bool WakeWordGate::wake_phrase_match_(const std::string& s,
                                      std::string& stripped) const {
    std::smatch m;
    if (!std::regex_search(s, m, wake_re_)) return false;
    // Only accept matches at the very start so a stray "hecquin" inside
    // a song lyric or a longer sentence doesn't open the wake window.
    if (m.position(0) != 0) return false;
    stripped = s.substr(static_cast<std::size_t>(m.position(0)) + m.length(0));
    // Trim leading spaces / commas left over from the strip.
    while (!stripped.empty() &&
           (stripped.front() == ' ' || stripped.front() == ',' ||
            stripped.front() == '.' || stripped.front() == '!')) {
        stripped.erase(stripped.begin());
    }
    return true;
}

WakeWordGate::Decision WakeWordGate::decide(const std::string& transcript,
                                            Clock::time_point now) {
    Decision d;
    d.transcript = transcript;
    const Mode m = mode_.load(std::memory_order_acquire);

    if (m == Mode::Always) {
        d.route = true;
        return d;
    }

    if (m == Mode::Ptt) {
        d.route = ptt_pressed_.load(std::memory_order_acquire);
        return d;
    }

    // wake_word: a transcript routes if either:
    //   - it starts with a wake phrase (which we strip),
    //   - or it arrived within `wake_window_ms` of a previous wake
    //     phrase (so the user can chain follow-ups without re-saying
    //     the phrase every turn).
    std::string stripped;
    if (wake_phrase_match_(transcript, stripped)) {
        last_wake_at_.store(now.time_since_epoch().count(),
                            std::memory_order_release);
        d.route = !stripped.empty();
        d.transcript = std::move(stripped);
        return d;
    }

    const auto last_count = last_wake_at_.load(std::memory_order_acquire);
    if (last_count != 0) {
        const auto last = Clock::time_point(Clock::duration(last_count));
        if (now - last < std::chrono::milliseconds(wake_window_ms_)) {
            d.route = true;
            return d;
        }
    }

    d.route = false;
    return d;
}

} // namespace hecquin::voice
