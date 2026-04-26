#pragma once

namespace hecquin::tts {

/**
 * Native sample rate of every Piper voice model we ship and stream.
 *
 * Hoisted into one header so the playback pipeline, the buffered SDL
 * player, and both Piper backends can not drift apart.  Anyone tempted
 * to hard-code 22050 elsewhere should pull this in instead.
 */
inline constexpr int kPiperSampleRate = 22050;

} // namespace hecquin::tts
