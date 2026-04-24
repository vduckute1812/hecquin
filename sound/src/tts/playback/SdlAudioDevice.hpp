#pragma once

#include <SDL.h>

namespace hecquin::tts::playback {

/**
 * Make sure the SDL audio subsystem is initialized.  Safe to call when
 * another component (e.g. `AudioCapture`) has already initialized it —
 * this never calls `SDL_Quit`.
 *
 * Returns false and logs the SDL error if initialization fails.
 */
bool ensure_audio_init();

/**
 * Low-level wrapper around `SDL_OpenAudioDevice` for the mono int16
 * Piper use-case.  Returns 0 on failure, which matches SDL's
 * "invalid device id" convention.
 */
SDL_AudioDeviceID open_mono_s16_device(SDL_AudioCallback callback,
                                       void* userdata,
                                       int sample_rate,
                                       int buffer_samples);

} // namespace hecquin::tts::playback
