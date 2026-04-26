#include "tts/playback/StreamingSdlPlayer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>

namespace hecquin::tts::playback {

namespace {

constexpr int kAudioBufferSamples = 4096;

inline std::int16_t saturate_i16(float v) {
    if (v >  32767.0f) return  32767;
    if (v < -32768.0f) return -32768;
    return static_cast<std::int16_t>(v);
}

} // namespace

void StreamingSdlPlayer::sdl_callback_(void* userdata,
                                       std::uint8_t* stream,
                                       int len) {
    auto* player = static_cast<StreamingSdlPlayer*>(userdata);
    player->queue_.pop_into(stream, len);
    if (len > 0) {
        const std::size_t n =
            static_cast<std::size_t>(len) / sizeof(std::int16_t);
        player->apply_gain_(reinterpret_cast<std::int16_t*>(stream), n);
    }
}

void StreamingSdlPlayer::apply_gain(std::int16_t* samples, std::size_t n,
                                    int sample_rate, float target, int ramp_ms,
                                    float& current_gain,
                                    float& ramp_step,
                                    float& ramp_for_target) {
    // Only (re)compute the per-sample step when the *target* changes.
    // Once a step is locked in we keep applying it across buffers so
    // the ramp actually converges instead of asymptotically chasing
    // its tail (recomputing delta/ramp_samples every buffer would
    // spread the *remaining* gap over a fresh full ramp_ms).
    if (target != ramp_for_target) {
        const float delta = target - current_gain;
        const float ramp_samples =
            (sample_rate > 0 && ramp_ms > 0)
                ? static_cast<float>(sample_rate) *
                      static_cast<float>(ramp_ms) / 1000.0f
                : static_cast<float>(n > 0 ? n : 1);
        ramp_step = (ramp_samples > 0.0f) ? delta / ramp_samples : delta;
        ramp_for_target = target;
    }
    if (current_gain == 1.0f && target == 1.0f) return;
    for (std::size_t i = 0; i < n; ++i) {
        samples[i] = saturate_i16(static_cast<float>(samples[i]) * current_gain);
        if (current_gain != target) {
            current_gain += ramp_step;
            if ((ramp_step > 0.0f && current_gain >= target) ||
                (ramp_step < 0.0f && current_gain <= target) ||
                ramp_step == 0.0f) {
                current_gain = target;
                ramp_step = 0.0f;
            }
        }
    }
}

void StreamingSdlPlayer::apply_gain_(std::int16_t* samples, std::size_t n) {
    const float target  = target_gain_.load(std::memory_order_acquire);
    const int   ramp_ms = target_ramp_ms_.load(std::memory_order_acquire);
    apply_gain(samples, n, sample_rate_, target, ramp_ms,
               current_gain_, ramp_step_, ramp_for_target_);
}

void StreamingSdlPlayer::set_gain_target(float linear, int ramp_ms) {
    if (linear < 0.0f) linear = 0.0f;
    if (ramp_ms  < 0)   ramp_ms = 0;
    // Publish target + ramp duration; the audio callback recomputes
    // its per-sample step from these on the next buffer.
    target_ramp_ms_.store(ramp_ms, std::memory_order_release);
    target_gain_.store(linear, std::memory_order_release);
}

StreamingSdlPlayer::~StreamingSdlPlayer() {
    stop();
}

bool StreamingSdlPlayer::start(int sample_rate) {
    if (!device_.open(&StreamingSdlPlayer::sdl_callback_, this,
                      sample_rate, kAudioBufferSamples)) {
        return false;
    }
    // Cushion the callback against synth startup latency (~62 ms at
    // 22050 Hz) so the very first frame is not pure silence.
    prebuffer_samples_ = static_cast<std::size_t>(sample_rate) / 16;
    sample_rate_ = sample_rate;
    // Reset gain state so a recycled player instance doesn't inherit
    // a duck/un-duck ramp from a previous track.
    target_gain_.store(1.0f, std::memory_order_release);
    target_ramp_ms_.store(0, std::memory_order_release);
    current_gain_    = 1.0f;
    ramp_step_       = 0.0f;
    ramp_for_target_ = 1.0f;
    return true;
}

void StreamingSdlPlayer::push(const std::int16_t* data, std::size_t n_samples) {
    if (!device_.is_open()) return;
    queue_.push(data, n_samples);
    if (!started_ && queue_.size() >= prebuffer_samples_) {
        device_.set_paused(false);
        started_ = true;
    }
}

void StreamingSdlPlayer::finish() {
    if (!device_.is_open()) return;
    queue_.mark_eof();
    if (!started_) {
        device_.set_paused(false);
        started_ = true;
    }
}

void StreamingSdlPlayer::wait_until_drained() {
    if (!device_.is_open()) return;
    queue_.wait_until_drained();
    // Let the DAC drain before we yank the device.  SDL has no public
    // "callback finished" hook, and the cv signals as soon as the
    // *queue* is empty — the hardware buffer can still be playing the
    // last frame for ~ one period after that.
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
}

bool StreamingSdlPlayer::set_paused(bool paused) {
    if (!device_.is_open()) return false;
    device_.set_paused(paused);
    // `started_` tracks whether playback has been kicked off at all;
    // a pause should not roll that bit back, otherwise resuming would
    // re-trigger the prebuffer threshold logic in `push()`.
    if (!paused) started_ = true;
    return true;
}

void StreamingSdlPlayer::stop() {
    device_.close();
}

} // namespace hecquin::tts::playback
