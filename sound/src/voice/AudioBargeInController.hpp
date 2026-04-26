#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>

namespace hecquin::voice {

/**
 * Coordinates the audio side effects of incoming-voice barge-in:
 *
 *   - while music is playing, ducks the SDL output gain on detected
 *     voice and ramps it back up on silence (with attack / release /
 *     hold timing so brief blips don't thrash the output);
 *   - while the assistant is speaking, aborts the in-flight TTS so the
 *     user's utterance can be collected immediately.
 *
 * The controller owns no audio devices itself — it holds two
 * `std::function` sinks the listener wires up at start-up:
 *
 *   - `GainSetter`: forwarded to the active music player's
 *     `StreamingSdlPlayer::set_gain_target(linear, ramp_ms)`.
 *   - `AbortFn`: fires a one-shot abort flag in the TTS path so the
 *     piper streaming loop bails on its next sample callback.
 *
 * Voice transitions are reported by `UtteranceCollector` via
 * `on_voice_state_change(bool)`; the listener's poll loop drives
 * `tick(now)` once per ~50 ms so deferred un-duck (after the hold
 * timer expires) is honoured without the controller spinning up its
 * own thread.
 *
 * Thread model: every public method may be called from the listener
 * thread.  The atomics expose live state to readers on other threads
 * (e.g. tests / future telemetry); writes happen on the listener
 * thread.  The sink callbacks are dispatched on the listener thread.
 */
class AudioBargeInController {
public:
    struct Config {
        /** Linear gain applied to the music output while voice is
         *  active.  0.25 ≈ -12 dB. */
        float music_duck_gain = 0.25f;
        /** Ramp duration when ducking down. */
        int   attack_ms       = 30;
        /** Ramp duration when ramping back up to 1.0. */
        int   release_ms      = 250;
        /** Time after voice goes silent before we start the release
         *  ramp.  Avoids stuttering on inter-word gaps. */
        int   hold_ms         = 200;
        /** When false, TTS abort is disabled (the legacy `MuteGuard`
         *  path takes over and the assistant always finishes its
         *  reply). */
        bool  tts_barge_in_enabled = true;
        /** Multiplier the collector applies to its start / continue
         *  thresholds while TTS is active.  Stored here so the
         *  listener can copy it onto `VoiceListenerConfig`. */
        float tts_threshold_boost  = 2.0f;

        /**
         * Override defaults from env vars:
         *   HECQUIN_DUCK_GAIN_DB         (default -12 dB)
         *   HECQUIN_DUCK_ATTACK_MS       (default 30)
         *   HECQUIN_DUCK_RELEASE_MS      (default 250)
         *   HECQUIN_DUCK_HOLD_MS         (default 200)
         *   HECQUIN_TTS_BARGE_IN         (1 = on, 0 = legacy MuteGuard)
         *   HECQUIN_TTS_THRESHOLD_BOOST  (default 2.0)
         */
        void apply_env_overrides();
    };

    using GainSetter = std::function<void(float linear, int ramp_ms)>;
    using AbortFn    = std::function<void()>;
    using Clock      = std::chrono::steady_clock;

    AudioBargeInController();
    explicit AudioBargeInController(Config cfg);

    const Config& config() const { return cfg_; }

    void set_music_gain_setter(GainSetter cb);
    void set_tts_aborter(AbortFn cb);

    /** Listener flips this on `MusicSideEffects::on_playback_started`
     *  / `on_resume` (true) and on `on_cancel` / `on_pause` (false).
     *  Going false force-unducks (no hold) so silence after a stopped
     *  song is not artificially attenuated for the next 200 ms. */
    void set_music_active(bool active);

    /** TTS player flips this true while speaking.  When the boost is
     *  enabled the listener also informs `UtteranceCollector::set_tts_active`
     *  so per-frame thresholds are raised — but that wiring is the
     *  caller's responsibility, not this class's. */
    void set_tts_active(bool active);

    /** Hooked from `UtteranceCollector::set_voice_state_callback`.
     *  Edge-triggered: the collector emits exactly once per ON/OFF
     *  flip. */
    void on_voice_state_change(bool voice);

    /** Driven from the listener's poll loop.  Honours the hold timer
     *  (releases the duck after `hold_ms` of continuous silence) so
     *  the controller doesn't need its own thread. */
    void tick(Clock::time_point now);

    /** Diagnostics / tests: true while the music gain is currently
     *  ducked or holding. */
    bool ducking() const { return ducking_.load(std::memory_order_acquire); }

    bool tts_barge_in_enabled() const { return cfg_.tts_barge_in_enabled; }
    float tts_threshold_boost() const { return cfg_.tts_threshold_boost; }

private:
    void duck_();
    void unduck_now_();
    /** Dispatch the gain setter under the sink mutex (cheap; the
     *  listener thread is the only writer to the sink). */
    void emit_gain_(float linear, int ramp_ms);
    /** Dispatch the TTS abort (idempotent — controller fires it at
     *  most once per `set_tts_active(true)` window). */
    void emit_abort_();

    Config cfg_;

    /** Sinks; protected by `sink_mu_` so set_*_setter from one thread
     *  doesn't race with a dispatch from the listener thread.  Held
     *  briefly only — the actual sink call happens after a copy. */
    mutable std::mutex sink_mu_;
    GainSetter gain_setter_;
    AbortFn    tts_aborter_;

    std::atomic<bool> music_active_{false};
    std::atomic<bool> tts_active_{false};
    std::atomic<bool> voice_{false};
    std::atomic<bool> ducking_{false};
    /** True while we've entered the hold window: voice is silent but
     *  we haven't released the duck yet.  Lives only on the listener
     *  thread. */
    bool holding_ = false;
    Clock::time_point release_at_{};
    /** Snapshots whether we already fired the abort for the current
     *  `set_tts_active(true)` window so a stutter of voice ON/OFF
     *  edges doesn't issue duplicate aborts. */
    bool tts_abort_fired_ = false;
};

} // namespace hecquin::voice
