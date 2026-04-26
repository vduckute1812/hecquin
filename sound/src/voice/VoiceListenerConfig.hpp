#pragma once

/**
 * Tuning knobs for the listener's primary VAD + secondary gate.
 *
 * Split out of `voice/VoiceListener.hpp` (which had grown to ~360 lines
 * mixing config, callbacks, telemetry, and the listener class itself)
 * so call sites that only need to populate / mutate the config don't
 * pull in the whole listener pipeline.  `VoiceListener.hpp` re-exports
 * this header so existing includes keep compiling.
 */
struct VoiceListenerConfig {
    int vad_window_samples = 512;
    int min_speech_ms = 500;
    int end_silence_ms = 800;
    float voice_rms_threshold = 0.02f;
    int poll_interval_ms = 50;
    int buffer_max_seconds = 30;
    int buffer_keep_seconds = 10;
    // Fraction of poll frames during collection that must register as voiced
    // (denominator excludes the trailing end-silence tail so short
    // utterances aren't unfairly punished).  Low-energy rustling or brief
    // music spikes rarely exceed ~20 %; actual speech is typically 60–95 %.
    // Set to 0 to disable.
    float min_voiced_frame_ratio = 0.30f;
    // Minimum mean-RMS over the whole collected utterance.  Rejects whispers
    // and faint background chatter that briefly crossed the VAD threshold.
    float min_utterance_rms = 0.015f;

    // Adaptive VAD (auto-tune to the user's mic / room)
    /**
     * When true (default), `UtteranceCollector` measures the ambient
     * noise floor at startup and derives the start / continue / utterance
     * thresholds as multiples of it.  Set to false (or `HECQUIN_VAD_AUTO=0`)
     * to use the static `voice_rms_threshold` / `min_utterance_rms`.
     */
    bool auto_calibrate = true;
    /**
     * When true, the noise floor keeps tracking idle (non-collecting)
     * frames after calibration so the gate self-corrects when the room
     * gets noisier or quieter.  Disable to freeze thresholds after the
     * initial calibration window.
     */
    bool auto_adapt = true;
    /** start_threshold = clamp(k_start * noise_floor, ...). */
    float k_start = 3.0f;
    /** continue_threshold = k_continue * start_threshold (hysteresis). */
    float k_continue = 0.6f;
    /** min_utterance_rms = clamp(k_utt * noise_floor, ...). */
    float k_utt = 2.0f;
    /** Calibration window length; truncated to >= 1 poll frame internally. */
    int calibration_ms = 1000;
    /** EMA smoothing factor for runtime idle-frame updates (0..1). */
    float ema_alpha = 0.05f;
    /**
     * Hard clamps so derived thresholds never wander outside sane bounds.
     *
     * `adaptive_min_start_thr` is the dominant knob in quiet rooms: when
     * the calibrated floor is tiny the start threshold clamps up to this
     * value, and the hysteresis continue threshold becomes
     * `k_continue * adaptive_min_start_thr`.  At 0.005 the resulting
     * continue threshold (~0.003 with default `k_continue = 0.6`) sits
     * comfortably above the ambient peaks typical of a quiet desk
     * (~0.001-0.0025 RMS) so the silence timer can fire reliably.
     */
    float adaptive_min_start_thr = 0.005f;
    float adaptive_max_start_thr = 0.10f;
    float adaptive_min_utt_rms = 0.002f;
    float adaptive_max_utt_rms = 0.08f;
    /**
     * Defense-in-depth cap on a single utterance.  When `frame_rms`
     * stays above the continue threshold indefinitely (e.g. a noisy
     * environment that the noise floor hasn't caught up with, or
     * persistent music on the channel) the collector force-closes
     * after this many milliseconds and lets the secondary gate
     * decide.  Set to 0 to disable the safety net.
     */
    int max_utterance_ms = 15000;
    /** When true, the collector logs the live noise floor + thresholds. */
    bool debug = false;

    /**
     * Multiplier applied to the per-frame start / continue thresholds
     * while the assistant is speaking (i.e. while the TTS path has
     * called `UtteranceCollector::set_tts_active(true)`).  The mic
     * stays live so the user can barge in, but the assistant's own
     * bleed has to be ~boost× louder than ambient to count as voice.
     * Set to 1.0 to disable the boost (mic equally sensitive while
     * speaking — useful only with hardware AEC).
     */
    float tts_threshold_boost = 2.0f;

    /**
     * Set by `apply_env_overrides()` when the user pinned a specific
     * threshold via `HECQUIN_VAD_VOICE_RMS_THRESHOLD` /
     * `HECQUIN_VAD_MIN_UTTERANCE_RMS`.  Pinned fields bypass the
     * adaptive logic — other fields keep auto-tuning.
     */
    bool voice_rms_threshold_pinned = false;
    bool min_utterance_rms_pinned = false;

    /**
     * Override thresholds from `HECQUIN_VAD_*` env vars so users can tune the
     * gate for their mic / room without rebuilding:
     *   HECQUIN_VAD_VOICE_RMS_THRESHOLD   pin start threshold (default 0.02)
     *   HECQUIN_VAD_MIN_VOICED_RATIO      (default 0.30, 0 disables)
     *   HECQUIN_VAD_MIN_UTTERANCE_RMS     pin secondary-gate min RMS (default 0.015, 0 disables)
     *   HECQUIN_VAD_AUTO                  "0" / "false" disables auto-tuning entirely
     *   HECQUIN_VAD_K_START               start_threshold multiplier (default 3.0)
     *   HECQUIN_VAD_K_CONTINUE            hysteresis multiplier (default 0.6)
     *   HECQUIN_VAD_K_UTT                 min-utterance-rms multiplier (default 2.0)
     *   HECQUIN_VAD_MIN_START_THR         lower clamp on adaptive start threshold (default 0.005)
     *   HECQUIN_VAD_MAX_UTTERANCE_MS      hard cap on one recording (default 15000, 0 disables)
     *   HECQUIN_VAD_DEBUG                 "1" prints live floor + thresholds
     */
    void apply_env_overrides();
};
