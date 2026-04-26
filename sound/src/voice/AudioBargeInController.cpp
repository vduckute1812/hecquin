#include "voice/AudioBargeInController.hpp"

#include "common/EnvParse.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hecquin::voice {

namespace {

float db_to_linear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

} // namespace

void AudioBargeInController::Config::apply_env_overrides() {
    namespace env = hecquin::common::env;
    float fv = 0.0f;
    if (env::parse_float("HECQUIN_DUCK_GAIN_DB", fv)) {
        // Clamp to a sane range: -60 dB (effectively silent) to 0 dB
        // (no attenuation).  Positive values would *boost* music when
        // the user starts talking; not what anyone wants.
        const float clamped = std::clamp(fv, -60.0f, 0.0f);
        music_duck_gain = db_to_linear(clamped);
    }
    int iv = 0;
    if (env::parse_int("HECQUIN_DUCK_ATTACK_MS", iv))  attack_ms  = std::max(0, iv);
    if (env::parse_int("HECQUIN_DUCK_RELEASE_MS", iv)) release_ms = std::max(0, iv);
    if (env::parse_int("HECQUIN_DUCK_HOLD_MS", iv))    hold_ms    = std::max(0, iv);
    bool flag = false;
    if (env::parse_bool("HECQUIN_TTS_BARGE_IN", flag)) tts_barge_in_enabled = flag;
    if (env::parse_float("HECQUIN_TTS_THRESHOLD_BOOST", fv)) {
        tts_threshold_boost = std::max(1.0f, fv);
    }
}

AudioBargeInController::AudioBargeInController() : AudioBargeInController(Config{}) {}
AudioBargeInController::AudioBargeInController(Config cfg) : cfg_(std::move(cfg)) {}

void AudioBargeInController::set_music_gain_setter(GainSetter cb) {
    std::lock_guard<std::mutex> lk(sink_mu_);
    gain_setter_ = std::move(cb);
}

void AudioBargeInController::set_tts_aborter(AbortFn cb) {
    std::lock_guard<std::mutex> lk(sink_mu_);
    tts_aborter_ = std::move(cb);
}

void AudioBargeInController::set_music_active(bool active) {
    music_active_.store(active, std::memory_order_release);
    if (!active) {
        // Music stopped: drop any pending hold and force gain back to
        // unity immediately so a future track doesn't start in a
        // ducked state.  Cheap and idempotent.
        holding_ = false;
        if (ducking_.load(std::memory_order_acquire)) {
            ducking_.store(false, std::memory_order_release);
            emit_gain_(1.0f, 0);
        }
    }
}

void AudioBargeInController::set_tts_active(bool active) {
    tts_active_.store(active, std::memory_order_release);
    if (!active) {
        // Reset the one-shot fuse so the next TTS utterance can be
        // aborted on its own merit.
        tts_abort_fired_ = false;
    }
}

void AudioBargeInController::on_voice_state_change(bool voice) {
    voice_.store(voice, std::memory_order_release);

    if (voice) {
        // Voice ON: cancel any pending release + duck immediately.
        holding_ = false;
        if (music_active_.load(std::memory_order_acquire)) {
            duck_();
        }
        if (cfg_.tts_barge_in_enabled &&
            tts_active_.load(std::memory_order_acquire) &&
            !tts_abort_fired_) {
            tts_abort_fired_ = true;
            emit_abort_();
        }
        return;
    }

    // Voice OFF: arm the hold timer so a brief inter-word silence
    // doesn't bounce the gain back up immediately.  `tick` will
    // un-duck once the hold expires.
    if (ducking_.load(std::memory_order_acquire)) {
        holding_   = true;
        release_at_ = Clock::now() +
                      std::chrono::milliseconds(std::max(0, cfg_.hold_ms));
    }
}

void AudioBargeInController::tick(Clock::time_point now) {
    if (!holding_) return;
    if (now < release_at_) return;
    holding_ = false;
    unduck_now_();
}

void AudioBargeInController::duck_() {
    if (ducking_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    emit_gain_(cfg_.music_duck_gain, cfg_.attack_ms);
}

void AudioBargeInController::unduck_now_() {
    if (!ducking_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    emit_gain_(1.0f, cfg_.release_ms);
}

void AudioBargeInController::emit_gain_(float linear, int ramp_ms) {
    GainSetter cb;
    {
        std::lock_guard<std::mutex> lk(sink_mu_);
        cb = gain_setter_;
    }
    if (cb) cb(linear, ramp_ms);
}

void AudioBargeInController::emit_abort_() {
    AbortFn cb;
    {
        std::lock_guard<std::mutex> lk(sink_mu_);
        cb = tts_aborter_;
    }
    if (cb) cb();
}

} // namespace hecquin::voice
