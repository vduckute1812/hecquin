#include "tts/playback/SdlAudioDevice.hpp"

#include <iostream>

namespace hecquin::tts::playback {

bool ensure_audio_init() {
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0) {
        return true;
    }
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL_Init error: " << SDL_GetError() << std::endl;
        return false;
    }
    return true;
}

SDL_AudioDeviceID open_mono_s16_device(SDL_AudioCallback callback,
                                       void* userdata,
                                       int sample_rate,
                                       int buffer_samples) {
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = static_cast<Uint16>(buffer_samples);
    want.callback = callback;
    want.userdata = userdata;

    const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, SDL_FALSE, &want, nullptr, 0);
    if (dev == 0) {
        std::cerr << "Failed to open audio device: " << SDL_GetError() << std::endl;
    }
    return dev;
}

} // namespace hecquin::tts::playback
