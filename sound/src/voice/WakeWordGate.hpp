#pragma once

#include <atomic>
#include <chrono>
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
 * Thread model: every method is safe to call concurrently.  Wake-window
 * timestamps are updated under `std::atomic`.
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

private:
    bool wake_phrase_match_(const std::string& s, std::string& stripped) const;

    std::atomic<Mode> mode_{Mode::Always};
    std::atomic<bool> ptt_pressed_{false};

    /// Time of the most recent successful wake-phrase match.
    std::atomic<Clock::rep> last_wake_at_{};

    /// How long after a wake phrase non-prefixed utterances still route.
    int wake_window_ms_ = 8000;

    std::regex wake_re_;
    std::string wake_re_src_; // for debug + comparison
};

} // namespace hecquin::voice
