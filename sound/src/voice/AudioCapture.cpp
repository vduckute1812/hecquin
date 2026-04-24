#include "voice/AudioCapture.hpp"

#include <cmath>
#include <iostream>

void SDLCALL AudioCapture::sdlCallback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<AudioCapture*>(userdata);
    if (!self->keep_capturing_ || !self->keep_capturing_->load()) {
        return;
    }

    auto* samples = reinterpret_cast<float*>(stream);
    const int n = len / static_cast<int>(sizeof(float));

    std::lock_guard<std::mutex> lock(self->buffer_mutex_);
    self->buffer_.insert(self->buffer_.end(), samples, samples + n);
}

bool AudioCapture::open(std::atomic<bool>& keep_capturing, const AudioCaptureConfig& cfg) {
    keep_capturing_ = &keep_capturing;
    cfg_ = cfg;

    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL_Init error: " << SDL_GetError() << std::endl;
        return false;
    }

    const int num_devices = SDL_GetNumAudioDevices(SDL_TRUE);
    std::cout << "Found " << num_devices << " recording device(s):" << std::endl;
    for (int i = 0; i < num_devices; ++i) {
        std::cout << "  [" << i << "] " << SDL_GetAudioDeviceName(i, SDL_TRUE) << std::endl;
    }

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = cfg_.sample_rate;
    want.format = AUDIO_F32;
    want.channels = static_cast<Uint8>(cfg_.channels);
    want.samples = static_cast<Uint16>(cfg_.sdl_buffer_samples);
    want.callback = sdlCallback;
    want.userdata = this;

    const char* device_name = nullptr;
    if (cfg_.device_index >= 0 && cfg_.device_index < num_devices) {
        device_name = SDL_GetAudioDeviceName(cfg_.device_index, SDL_TRUE);
        std::cout << "→ Selected device [" << cfg_.device_index << "] " << device_name << std::endl;
    } else if (cfg_.device_index >= 0) {
        std::cerr << "AUDIO_DEVICE_INDEX=" << cfg_.device_index
                  << " out of range (0.." << num_devices - 1 << "), using default." << std::endl;
    }

    SDL_AudioSpec have;
    device_id_ = SDL_OpenAudioDevice(device_name, SDL_TRUE, &want, &have, 0);
    if (device_id_ == 0) {
        std::cerr << "Failed to open audio device: " << SDL_GetError() << std::endl;
        return false;
    }

    std::cout << "Audio device: " << have.freq << "Hz, " << static_cast<int>(have.channels) << " channels, format="
              << have.format << std::endl;
    return true;
}

void AudioCapture::close() {
    if (device_id_ != 0) {
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
    }
}

void AudioCapture::pauseDevice() {
    if (device_id_ != 0) {
        SDL_PauseAudioDevice(device_id_, 1);
    }
}

void AudioCapture::resumeDevice() {
    if (device_id_ != 0) {
        SDL_PauseAudioDevice(device_id_, 0);
    }
}

void AudioCapture::clearBuffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    buffer_.clear();
}

std::vector<float> AudioCapture::snapshotBuffer() const {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    return buffer_;
}

std::size_t AudioCapture::snapshotRecent(std::size_t max_samples,
                                         std::vector<float>& out) const {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    const std::size_t n = std::min(max_samples, buffer_.size());
    out.resize(n);
    if (n == 0) return 0;
    // Copy the last `n` samples (the window VAD actually inspects).
    const auto begin = buffer_.end() - static_cast<std::ptrdiff_t>(n);
    std::copy(begin, buffer_.end(), out.begin());
    return n;
}

std::size_t AudioCapture::bufferSize() const {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    return buffer_.size();
}

void AudioCapture::limitBufferSize(int max_seconds, int keep_seconds) {
    const size_t max_samples = static_cast<size_t>(cfg_.sample_rate * max_seconds);
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    if (buffer_.size() <= max_samples) {
        return;
    }
    const size_t keep_samples = static_cast<size_t>(cfg_.sample_rate * keep_seconds);
    const size_t excess = buffer_.size() - keep_samples;
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(excess));
}
