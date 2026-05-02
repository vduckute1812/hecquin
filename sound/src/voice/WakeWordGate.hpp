#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <regex>
#include <string>

namespace hecquin::voice {

/**
 * Wake-word / push-to-talk gate.  Decides whether a freshly transcribed
 * utterance should be routed to the rest of the pipeline.
 *
 * Three modes (configured via `HECQUIN_WAKE_MODE`):
 *
 *   - `always`   : every transcript routes (default; matches the
 *                  pre-existing behaviour).
 *   - `wake_word`: only transcripts that start with — or arrive within
 *                  `wake_window_ms` of — a previous wake-phrase
 *                  detection are routed.  The wake phrase itself is
 *                  consumed (stripped from the transcript) before the
 *                  router sees it.
 *   - `ptt`      : push-to-talk.  Transcripts only route while a
 *                  hardware GPIO pin (or a manual `set_ptt_pressed`
 *                  call from tests) is held down.
 *
 * The class is stateless w.r.t. the listener — the listener calls
 * `prepare_for_route(transcript)` and either gets back the transcript
 * to route (with the wake phrase stripped if any) or a `Skip` verdict
 * to drop the utterance silently.
 *
 * Thread model: `decide` and the simple setters (`set_mode`,
 * `set_ptt_pressed`) are safe to call concurrently.
 * `apply_env_overrides` mutates the wake regex + window and is also
 * safe to call concurrently with `decide` — both take a short mutex
 * around the regex match.  Mutations to `mode_` / `ptt_pressed_` /
 * `last_wake_at_` use atomics; the regex + source string are guarded
 * by `re_mu_` because `std::regex` is not copyable into an atomic.
 */
class WakeWordGate {
public:
    enum class Mode { Always, WakeWord, Ptt };
    using Clock = std::chrono::steady_clock;

    struct Decision {
        bool route = true;
        std::string transcript; // possibly with wake phrase stripped
    };

    WakeWordGate();

    /** Apply `HECQUIN_WAKE_MODE` / `HECQUIN_WAKE_WINDOW_MS` /
     *  `HECQUIN_WAKE_PHRASE` env knobs. */
    void apply_env_overrides();

    void set_mode(Mode m) { mode_.store(m, std::memory_order_release); }
    Mode mode() const { return mode_.load(std::memory_order_acquire); }

    /** Tests / scripted PTT integrations can flip this directly. */
    void set_ptt_pressed(bool pressed);
    bool ptt_pressed() const { return ptt_pressed_.load(std::memory_order_acquire); }

    /**
     * Decide whether `transcript` should be routed.  In `wake_word`
     * mode this also strips the wake phrase from the front (so the
     * downstream processor sees just the actual command).
     */
    [[nodiscard]] Decision decide(const std::string& transcript,
                                  Clock::time_point now = Clock::now());

    /**
     * Tests-only hook: install a custom wake regex source.  Returns
     * true on a clean compile, false (and leaves the previous regex
     * unchanged) on a regex_error.  Production code installs the
     * pattern through `HECQUIN_WAKE_PHRASE` / `apply_env_overrides`.
     */
    bool set_wake_pattern(const std::string& pattern);

    /** Tests-only: read back the installed pattern source. */
    std::string wake_pattern() const;

private:
    /** Wraps a `regex_search` call under `re_mu_`.  Returns true and
     *  populates `stripped` when the wake phrase matched at the start
     *  of `s`. */
    bool wake_phrase_match_(const std::string& s, std::string& stripped) const;

    std::atomic<Mode> mode_{Mode::Always};
    std::atomic<bool> ptt_pressed_{false};

    /// Time of the most recent successful wake-phrase match.
    std::atomic<Clock::rep> last_wake_at_{};

    /// How long after a wake phrase non-prefixed utterances still route.
    std::atomic<int> wake_window_ms_{8000};

    /// Mutex guarding `wake_re_` / `wake_re_src_`.  `std::regex` is
    /// not safe to mutate concurrently with `regex_search`; we hold
    /// the lock briefly across both reads (in `decide`) and writes
    /// (in `apply_env_overrides`).
    mutable std::mutex re_mu_;
    std::regex wake_re_;
    std::string wake_re_src_; // for debug + tests
};

} // namespace hecquin::voice
