#pragma once

#include <cstdint>
#include <vector>

namespace hecquin::tts::playback {

/**
 * Play a pre-synthesised mono int16 buffer at Piper's 22050 Hz using
 * SDL's default output device.  Blocks until playback finishes.  Safe
 * to call whether or not SDL audio is already initialized.
 *
 * Returns false if the device could not be opened.
 */
bool play_mono_22k(const std::vector<std::int16_t>& samples);

} // namespace hecquin::tts::playback
