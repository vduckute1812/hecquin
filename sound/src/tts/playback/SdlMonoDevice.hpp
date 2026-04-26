#pragma once

#include <SDL.h>

namespace hecquin::tts::playback {

/**
 * RAII handle around `SDL_OpenAudioDevice` for the mono int16 PCM
 * use-case shared by Piper TTS and the music streaming pipeline.
 *
 * Splits the device lifecycle out of `StreamingSdlPlayer` so the
 * player can focus on the queue / drain / pause state machine.  The
 * underlying `open_mono_s16_device` / `ensure_audio_init` helpers in
 * `SdlAudioDevice.hpp` keep their free-function form for callers that
 * just need a one-shot open without RAII semantics.
 */
class SdlMonoDevice {
public:
    SdlMonoDevice() = default;
    ~SdlMonoDevice() { close(); }

    SdlMonoDevice(const SdlMonoDevice&) = delete;
    SdlMonoDevice& operator=(const SdlMonoDevice&) = delete;

    /** Open the device.  Returns false on `SDL_Init` / open failure. */
    bool open(SDL_AudioCallback callback,
              void* userdata,
              int sample_rate,
              int buffer_samples);

    /** Suspend / resume playback without releasing the handle. */
    void set_paused(bool paused);

    /** Close the device.  Idempotent. */
    void close();

    bool is_open() const noexcept { return dev_ != 0; }
    SDL_AudioDeviceID id() const noexcept { return dev_; }

private:
    SDL_AudioDeviceID dev_ = 0;
};

} // namespace hecquin::tts::playback
