#pragma once

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
 */
bool speak_and_play_streaming(const std::string& text,
                              const std::string& model_path);

} // namespace hecquin::tts
