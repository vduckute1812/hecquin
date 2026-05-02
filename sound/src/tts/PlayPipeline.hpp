#pragma once

#include <atomic>
#include <string>

namespace hecquin::tts {

/**
 * Implementation of the synth + playback pipelines exposed by the flat
 * `PiperSpeech.hpp` C-style facade.  Lifted out so `PiperSpeech.cpp`
 * can stay a single-screen-of-code dispatcher to the real subsystem
 * (`tts/backend/`, `tts/playback/`).
 *
 * Both functions log at INFO level on entry; failure paths print to
 * stderr.  No exceptions escape.  These are not part of the public
 * API — callers should use the thin wrappers in `tts/PiperSpeech.hpp`.
 */

namespace detail {

/** Decision returned by the streaming-vs-buffered planner. */
enum class StreamingDecision {
    /** Keep streaming — both the player and the spawn handshake worked. */
    Stream,
    /**
     * Bail out and re-run the buffered path (`speak_and_play`) so the
     * user still hears the reply.  Triggers: (a) `StreamingSdlPlayer`
     * could not open an SDL device, or (b) `run_pipe_synth` reported
     * `spawned == false`, meaning the Piper child / pipe never started.
     */
    FallbackToBuffered,
};

/**
 * Pure decision used by `speak_and_play_streaming` to choose between
 * the streaming and buffered playback paths.  Exposed so the fallback
 * contract can be unit-tested without spawning Piper or opening an
 * SDL device.
 */
constexpr StreamingDecision decide_streaming_path(bool streaming_player_started,
                                                  bool spawn_succeeded) {
    if (!streaming_player_started) return StreamingDecision::FallbackToBuffered;
    if (!spawn_succeeded)          return StreamingDecision::FallbackToBuffered;
    return StreamingDecision::Stream;
}

/**
 * Pure final-outcome resolver for the streaming path.  An aborted
 * stream always reports failure (callers asked for barge-in); a clean
 * stream defers to whatever `log_piper_wait_status` returned for the
 * spawned process.
 */
constexpr bool resolve_streaming_outcome(bool aborted, bool exit_status_ok) {
    return aborted ? false : exit_status_ok;
}

} // namespace detail

/** Buffered: synthesise the whole utterance, then hand off to SDL. */
bool speak_and_play(const std::string& text, const std::string& model_path);

/**
 * Streaming: pipe Piper output directly into SDL playback as soon as
 * the first samples arrive.  Falls back to `speak_and_play` on pipe
 * setup failure so callers always get an end-to-end result.
 *
 * `abort_flag` (optional, may be null) is polled per sample-callback;
 * when true the read loop returns false, the player is stopped
 * immediately (audio cut mid-sentence), and the spawn handle is reaped
 * so the function returns to the caller without waiting for piper to
 * close its pipe naturally.
 */
bool speak_and_play_streaming(const std::string& text,
                              const std::string& model_path,
                              const std::atomic<bool>* abort_flag = nullptr);

} // namespace hecquin::tts
