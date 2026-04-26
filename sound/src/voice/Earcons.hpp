#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace hecquin::voice {

/**
 * Tiny audio-cue collaborator.  Plays short synthesised tones (≤200 ms)
 * through SDL so the user gets immediate feedback for events that have
 * no spoken reply:
 *
 *   - `start_listening`  : utterance VAD opened (rising blip).
 *   - `vad_rejected`     : secondary gate dropped the utterance (soft
 *                          falling blip — "didn't catch that").
 *   - `thinking`         : pulsed at ~0.7 Hz while a long LLM call is in
 *                          flight (Tier-2 latency masking).
 *   - `network_offline`  : one-shot at boot when the cloud is
 *                          unavailable, and after a chain of HTTP 5xx /
 *                          timeout errors.
 *   - `acknowledge`      : immediate "got it" for `AbortReply` so the
 *                          user hears feedback while the assistant
 *                          stops mid-reply.
 *
 * Tones are generated in-process so no `.wav` assets need to ship.  The
 * implementation is best-effort: failures (no SDL audio device, output
 * busy) are silently swallowed so an earcon never crashes the listener.
 *
 * Optional WAV overrides are honoured if present at
 * `<earcons_dir>/<name>.wav` (mono int16, 22050 Hz) — useful for
 * polish without recompiling.  Set via `set_search_dir`.
 *
 * Thread model: every method is safe to call from any thread.  Earcons
 * play synchronously by default (cheap, blocks ~150 ms); `play_async`
 * dispatches on a detached thread for callers that can't afford the
 * stall (the noise-floor calibration pulse, the thinking loop).
 */
class Earcons {
public:
    /** Built-in cue identifiers.  Keep this list short — every cue is
     *  one more thing the user has to interpret. */
    enum class Cue {
        StartListening,
        VadRejected,
        Thinking,
        NetworkOffline,
        Acknowledge,
        Sleep,
        Wake,
    };

    Earcons();

    /** Disable the entire system.  Set by `apply_env_overrides()` when
     *  `HECQUIN_EARCONS=0` is in the environment. */
    void set_enabled(bool on) { enabled_.store(on, std::memory_order_release); }
    bool enabled() const { return enabled_.load(std::memory_order_acquire); }

    /**
     * Optional directory to search for `<name>.wav` overrides.  Empty
     * = use synthesised tones only.  Non-existent directory is fine.
     */
    void set_search_dir(std::string dir);

    /** Apply `HECQUIN_EARCONS` (`0` disables) and `HECQUIN_EARCONS_DIR`. */
    void apply_env_overrides();

    /** Play synchronously (blocks until the cue finishes, ~150 ms). */
    void play(Cue c);

    /** Dispatch on a detached thread.  No-op when disabled. */
    void play_async(Cue c);

    /**
     * Start the "thinking" pulse on a background thread.  Repeats
     * `Cue::Thinking` every ~1.4 s until `stop_thinking()` is called.
     * Idempotent: a second call while already running is a no-op.
     */
    void start_thinking();
    void stop_thinking();

private:
    /** Synthesise the PCM for `c` once (cached on first request). */
    const std::vector<std::int16_t>& pcm_for_(Cue c);

    /** Try to load `<dir>/<name>.wav` from disk.  Returns empty on miss. */
    std::vector<std::int16_t> try_load_override_(const char* name) const;

    std::atomic<bool> enabled_{true};
    std::string search_dir_;
    std::mutex cache_mu_;
    std::vector<std::int16_t> cache_[7]; // one entry per Cue

    std::atomic<bool> thinking_running_{false};
    std::atomic<bool> thinking_stop_{false};
};

} // namespace hecquin::voice
