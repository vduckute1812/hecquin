#include "voice/Earcons.hpp"

#include "common/EnvParse.hpp"
#include "tts/PiperSampleRate.hpp"
#include "tts/playback/BufferedSdlPlayer.hpp"
#include "tts/wav/WavReader.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>
#include <utility>

namespace hecquin::voice {

namespace {

constexpr int kSampleRate = hecquin::tts::kPiperSampleRate;
constexpr float kPi = 3.14159265358979323846f;

const char* cue_basename(Earcons::Cue c) {
    switch (c) {
        case Earcons::Cue::StartListening: return "start_listening";
        case Earcons::Cue::VadRejected:    return "vad_rejected";
        case Earcons::Cue::Thinking:       return "thinking";
        case Earcons::Cue::NetworkOffline: return "network_offline";
        case Earcons::Cue::Acknowledge:    return "acknowledge";
        case Earcons::Cue::Sleep:          return "sleep";
        case Earcons::Cue::Wake:           return "wake";
    }
    return "";
}

/** Two-tone sine burst with linear ADSR envelope. */
std::vector<std::int16_t> synth_two_tone(float f0_hz, float f1_hz,
                                          int duration_ms,
                                          float peak = 0.45f) {
    const int n = std::max(1, kSampleRate * duration_ms / 1000);
    std::vector<std::int16_t> out(static_cast<std::size_t>(n));
    const int attack = std::min(n / 8, kSampleRate / 100); // ~10 ms
    const int release = std::max(attack, n / 4);
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        // Linear glide between f0 and f1 — gives a tiny "blip" character
        // that's perceptibly different from a flat tone (and from speech).
        const float frac = static_cast<float>(i) / static_cast<float>(n);
        const float f = f0_hz + (f1_hz - f0_hz) * frac;
        float env = 1.0f;
        if (i < attack) env = static_cast<float>(i) / static_cast<float>(attack);
        else if (i > n - release) {
            env = static_cast<float>(n - i) / static_cast<float>(release);
        }
        const float s = std::sin(2.0f * kPi * f * t) * peak * env;
        const int v = static_cast<int>(s * 32767.0f);
        out[i] = static_cast<std::int16_t>(std::max(-32768, std::min(32767, v)));
    }
    return out;
}

std::vector<std::int16_t> synth_for(Earcons::Cue c) {
    switch (c) {
        case Earcons::Cue::StartListening: return synth_two_tone(660.0f, 990.0f, 90);
        case Earcons::Cue::VadRejected:    return synth_two_tone(440.0f, 220.0f, 120, 0.30f);
        case Earcons::Cue::Thinking:       return synth_two_tone(540.0f, 540.0f, 60, 0.20f);
        case Earcons::Cue::NetworkOffline: return synth_two_tone(330.0f, 165.0f, 250, 0.35f);
        case Earcons::Cue::Acknowledge:    return synth_two_tone(880.0f, 1320.0f, 70);
        case Earcons::Cue::Sleep:          return synth_two_tone(440.0f, 220.0f, 220, 0.30f);
        case Earcons::Cue::Wake:           return synth_two_tone(440.0f, 880.0f, 180);
    }
    return {};
}

} // namespace

Earcons::Earcons() = default;

void Earcons::set_search_dir(std::string dir) {
    std::lock_guard<std::mutex> lk(cache_mu_);
    search_dir_ = std::move(dir);
    for (auto& v : cache_) v.clear();
}

void Earcons::apply_env_overrides() {
    namespace env = hecquin::common::env;
    bool flag = false;
    if (env::parse_bool("HECQUIN_EARCONS", flag)) {
        set_enabled(flag);
    }
    if (const char* dir = env::read_string("HECQUIN_EARCONS_DIR")) {
        set_search_dir(dir);
    }
}

std::vector<std::int16_t> Earcons::try_load_override_(const char* name) const {
    if (search_dir_.empty()) return {};
    const std::string path = search_dir_ + "/" + name + ".wav";
    std::vector<std::int16_t> samples;
    if (!hecquin::tts::wav::read_pcm_s16_mono(path, samples)) return {};
    if (samples.empty()) return {};
    const int sr = hecquin::tts::wav::parse_sample_rate(path);
    if (sr != kSampleRate) {
        // Mismatched WAV — too risky to play (would sound chipmunk-ish).
        // Drop and fall back to the synthesised version; the user can
        // re-encode the override if they want it picked up.
        return {};
    }
    return samples;
}

const std::vector<std::int16_t>& Earcons::pcm_for_(Cue c) {
    std::lock_guard<std::mutex> lk(cache_mu_);
    auto& slot = cache_[static_cast<std::size_t>(c)];
    if (!slot.empty()) return slot;
    auto override_pcm = try_load_override_(cue_basename(c));
    slot = override_pcm.empty() ? synth_for(c) : std::move(override_pcm);
    return slot;
}

void Earcons::play(Cue c) {
    if (!enabled_.load(std::memory_order_acquire)) return;
    const auto& pcm = pcm_for_(c);
    if (pcm.empty()) return;
    (void)hecquin::tts::playback::play_mono_22k(pcm);
}

void Earcons::play_async(Cue c) {
    if (!enabled_.load(std::memory_order_acquire)) return;
    std::thread([this, c] { this->play(c); }).detach();
}

void Earcons::start_thinking() {
    if (!enabled_.load(std::memory_order_acquire)) return;
    if (thinking_running_.exchange(true, std::memory_order_acq_rel)) return;
    thinking_stop_.store(false, std::memory_order_release);
    std::thread([this] {
        while (!thinking_stop_.load(std::memory_order_acquire) &&
               enabled_.load(std::memory_order_acquire)) {
            this->play(Cue::Thinking);
            for (int i = 0; i < 14 && !thinking_stop_.load(std::memory_order_acquire); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        thinking_running_.store(false, std::memory_order_release);
    }).detach();
}

void Earcons::stop_thinking() {
    thinking_stop_.store(true, std::memory_order_release);
}

} // namespace hecquin::voice
