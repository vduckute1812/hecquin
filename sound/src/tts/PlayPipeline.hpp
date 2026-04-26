#pragma once

#include <atomic>
#include <string>

namespace hecquin::tts {

/**
 * Implementation of the synth + playback pipelines exposed by the flat
 * `PiperSpeech.hpp` C-style facade.  Lifted out so `PiperSpeech.cpp`
 * can stay a single-screen-of-code dispatcher to the real subsystem
 * (`tts/backend/*`, `tts/playback/*`).
 *
 * Both functions log at INFO level on entry; failure paths print to
 * stderr.  No exceptions escape.  These are not part of the public
 * API — callers should use the thin wrappers in `tts/PiperSpeech.hpp`.
 */

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
