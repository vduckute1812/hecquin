#include "tts/playback/BufferedSdlPlayer.hpp"

#include "tts/playback/SdlAudioDevice.hpp"

#include <SDL.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace hecquin::tts::playback {

namespace {

constexpr int kPiperSampleRate = 22050;
constexpr int kAudioBufferSamples = 4096;

struct BufferedAudioState {
    std::vector<std::int16_t> samples;
    std::size_t position = 0;
    bool finished = false;
};

void buffered_callback(void* userdata, Uint8* stream, int len) {
    auto* state = static_cast<BufferedAudioState*>(userdata);
    const int samples_to_copy = len / static_cast<int>(sizeof(std::int16_t));
    auto* out = reinterpret_cast<std::int16_t*>(stream);
    for (int i = 0; i < samples_to_copy; ++i) {
        if (state->position < state->samples.size()) {
            out[i] = state->samples[state->position++];
        } else {
            out[i] = 0;
            state->finished = true;
        }
    }
}

} // namespace

bool play_mono_22k(const std::vector<std::int16_t>& samples) {
    if (!ensure_audio_init()) {
        return false;
    }

    BufferedAudioState state;
    state.samples = samples;

    const SDL_AudioDeviceID dev = open_mono_s16_device(
        &buffered_callback, &state, kPiperSampleRate, kAudioBufferSamples);
    if (dev == 0) {
        return false;
    }

    std::cout << "🔊 Playing speech..." << std::endl;
    SDL_PauseAudioDevice(dev, 0);

    while (!state.finished) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // Small tail so the DAC drains before we yank the device.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    SDL_CloseAudioDevice(dev);
    return true;
}

} // namespace hecquin::tts::playback
