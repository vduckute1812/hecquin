#include "tts/playback/SdlMonoDevice.hpp"

#include "tts/playback/SdlAudioDevice.hpp"

namespace hecquin::tts::playback {

bool SdlMonoDevice::open(SDL_AudioCallback callback,
                         void* userdata,
                         int sample_rate,
                         int buffer_samples) {
    if (!ensure_audio_init()) return false;
    dev_ = open_mono_s16_device(callback, userdata, sample_rate, buffer_samples);
    return dev_ != 0;
}

void SdlMonoDevice::set_paused(bool paused) {
    if (dev_ != 0) {
        SDL_PauseAudioDevice(dev_, paused ? 1 : 0);
    }
}

void SdlMonoDevice::close() {
    if (dev_ != 0) {
        SDL_CloseAudioDevice(dev_);
        dev_ = 0;
    }
}

} // namespace hecquin::tts::playback
