#include "tts/playback/StreamingSdlPlayer.hpp"

#include <chrono>
#include <thread>

namespace hecquin::tts::playback {

namespace {

constexpr int kAudioBufferSamples = 4096;

} // namespace

void StreamingSdlPlayer::sdl_callback_(void* userdata,
                                       std::uint8_t* stream,
                                       int len) {
    auto* player = static_cast<StreamingSdlPlayer*>(userdata);
    player->queue_.pop_into(stream, len);
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
