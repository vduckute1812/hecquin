#include "tts/playback/StreamingSdlPlayer.hpp"

#include "tts/playback/SdlAudioDevice.hpp"

#include <SDL.h>

#include <chrono>
#include <thread>

namespace hecquin::tts::playback {

namespace {

constexpr int kAudioBufferSamples = 4096;

} // namespace

void streaming_callback(void* userdata, Uint8* stream, int len) {
    auto* player = static_cast<StreamingSdlPlayer*>(userdata);
    auto* out = reinterpret_cast<std::int16_t*>(stream);
    const int requested = len / static_cast<int>(sizeof(std::int16_t));

    std::unique_lock<std::mutex> lock(player->mu_);
    int filled = 0;
    while (filled < requested && !player->queue_.empty()) {
        out[filled++] = player->queue_.front();
        player->queue_.pop_front();
    }
    for (int i = filled; i < requested; ++i) out[i] = 0;
    if (player->eof_ && player->queue_.empty()) {
        player->done_.store(true, std::memory_order_release);
    }
}

StreamingSdlPlayer::~StreamingSdlPlayer() {
    stop();
}

bool StreamingSdlPlayer::start(int sample_rate) {
    if (!ensure_audio_init()) {
        return false;
    }
    dev_ = open_mono_s16_device(&streaming_callback, this, sample_rate, kAudioBufferSamples);
    if (dev_ == 0) {
        return false;
    }
    // Cushion the callback against synth startup latency (~62 ms at
    // 22050 Hz) so the very first frame is not pure silence.
    prebuffer_samples_ = static_cast<std::size_t>(sample_rate) / 16;
    return true;
}

void StreamingSdlPlayer::push(const std::int16_t* data, std::size_t n_samples) {
    if (dev_ == 0) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.insert(queue_.end(), data, data + n_samples);
    }
    if (!started_) {
        std::size_t queued = 0;
        {
            std::lock_guard<std::mutex> lock(mu_);
            queued = queue_.size();
        }
        if (queued >= prebuffer_samples_) {
            SDL_PauseAudioDevice(dev_, 0);
            started_ = true;
        }
    }
}

void StreamingSdlPlayer::finish() {
    if (dev_ == 0) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        eof_ = true;
    }
    if (!started_) {
        SDL_PauseAudioDevice(dev_, 0);
        started_ = true;
    }
}

void StreamingSdlPlayer::wait_until_drained() {
    if (dev_ == 0) return;
    while (!done_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Let the DAC drain before we yank the device.
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
}

void StreamingSdlPlayer::stop() {
    if (dev_ != 0) {
        SDL_CloseAudioDevice(dev_);
        dev_ = 0;
    }
}

} // namespace hecquin::tts::playback
