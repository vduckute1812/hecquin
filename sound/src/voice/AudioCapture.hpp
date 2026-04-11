#pragma once

#include <SDL.h>

#include <atomic>
#include <mutex>
#include <vector>

struct AudioCaptureConfig {
    int sample_rate = 16000;
    int channels = 1;
    int sdl_buffer_samples = 1024;
};

/**
 * SDL capture at float32 mono (Whisper-friendly). Callback and main thread share the buffer
 * under a mutex so concurrent read/write is defined.
 */
class AudioCapture {
public:
    AudioCapture() = default;
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    /** When `*keep_capturing` is false, the SDL callback stops appending samples. */
    bool open(std::atomic<bool>& keep_capturing, const AudioCaptureConfig& cfg = {});
    void close();

    void pauseDevice();
    void resumeDevice();

    void clearBuffer();

    /** Thread-safe copy of the live capture buffer (for VAD / utterance extraction). */
    std::vector<float> snapshotBuffer() const;

    /** Trim buffer if it grows beyond max_seconds of audio (keeps tail). */
    void limitBufferSize(int max_seconds, int keep_seconds);

    SDL_AudioDeviceID deviceId() const { return device_id_; }

private:
    static void SDLCALL sdlCallback(void* userdata, Uint8* stream, int len);

    SDL_AudioDeviceID device_id_{0};
    std::atomic<bool>* keep_capturing_{nullptr};
    AudioCaptureConfig cfg_{};
    mutable std::mutex buffer_mutex_;
    std::vector<float> buffer_;
};
