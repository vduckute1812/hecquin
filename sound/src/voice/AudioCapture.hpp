#pragma once

#include "voice/AudioCaptureConfig.hpp"

#include <SDL.h>

#include <atomic>
#include <cstddef>
#include <mutex>
#include <vector>

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

    /**
     * Copy only the trailing `max_samples` worth of captured audio into `out`
     * (or the whole buffer if smaller).  Reuses `out`'s storage so the
     * poll-loop VAD does not allocate on every tick.  The returned count is
     * what actually lives in `out` — callers should prefer this over
     * `snapshotBuffer()` when they only inspect a short window.
     */
    std::size_t snapshotRecent(std::size_t max_samples, std::vector<float>& out) const;

    /** Current live buffer size (samples). Thread-safe. */
    std::size_t bufferSize() const;

    /** Trim buffer if it grows beyond max_seconds of audio (keeps tail). */
    void limitBufferSize(int max_seconds, int keep_seconds);

    SDL_AudioDeviceID deviceId() const { return device_id_; }

    /**
     * Open-time sanity check: temporarily resume the device, wait
     * `duration_ms` for samples to arrive, then pause again and
     * clear whatever was captured.  Returns the number of samples
     * the SDL callback actually delivered during the window.
     *
     * Used by `VoiceApp::init` to detect the "AUDIO_DEVICE_INDEX=-1
     * picked a silent default" case that would otherwise show up only
     * as the noise-floor tracker timing out 2 s into calibration.
     * No-op (returns 0) when the device is not open.
     */
    std::size_t probeSignal(int duration_ms = 500);

    /**
     * RAII mute helper: pauses the device + clears the ring on construction,
     * clears + resumes on destruction.  Use this around any TTS playback
     * block so the mic cannot re-capture the speaker output as "voice".
     *
     *     {
     *         AudioCapture::MuteGuard mute(capture);
     *         piper_speak_and_play(reply, model);
     *     }  // mic resumes automatically, even on early return / exception
     */
    class MuteGuard {
    public:
        explicit MuteGuard(AudioCapture& cap) : cap_(&cap) {
            cap_->pauseDevice();
            cap_->clearBuffer();
        }
        MuteGuard(const MuteGuard&) = delete;
        MuteGuard& operator=(const MuteGuard&) = delete;
        ~MuteGuard() {
            if (cap_) {
                cap_->clearBuffer();
                cap_->resumeDevice();
            }
        }

    private:
        AudioCapture* cap_;
    };

private:
    static void SDLCALL sdlCallback(void* userdata, Uint8* stream, int len);

    SDL_AudioDeviceID device_id_{0};
    std::atomic<bool>* keep_capturing_{nullptr};
    AudioCaptureConfig cfg_{};
    mutable std::mutex buffer_mutex_;
    std::vector<float> buffer_;
};
