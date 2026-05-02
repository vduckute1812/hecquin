#include "voice/UtteranceCollector.hpp"

#include "voice/AudioCapture.hpp"
#include "voice/VoiceListener.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace hecquin::voice {

namespace {

NoiseFloorConfig make_floor_cfg(const VoiceListenerConfig& cfg) {
    NoiseFloorConfig nf;
    const int poll = std::max(1, cfg.poll_interval_ms);
    nf.calibration_samples =
        std::max(1, cfg.calibration_ms / poll);
    nf.ema_alpha = std::clamp(cfg.ema_alpha, 0.0f, 1.0f);
    // Seed the tracker just below the lower clamp on the start threshold
    // so that — even before calibration completes — the derived
    // `k_start * floor` lands in the user-tunable adaptive band.
    nf.seed_floor = std::max(1e-4f, cfg.adaptive_min_start_thr / std::max(1.0f, cfg.k_start));
    nf.min_floor = 1e-4f;
    nf.max_floor = std::max(0.05f, cfg.adaptive_max_start_thr);
    return nf;
}

} // namespace

UtteranceCollector::UtteranceCollector(AudioCapture& capture,
                                       const VoiceListenerConfig& cfg,
                                       const std::atomic<bool>& app_running)
    : capture_(capture),
      cfg_(cfg),
      app_running_(app_running),
      tracker_(make_floor_cfg(cfg)) {
    recompute_thresholds_();
}

float UtteranceCollector::rms(const std::vector<float>& samples,
                              std::size_t start, std::size_t end) {
    if (start >= end || end > samples.size()) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (std::size_t i = start; i < end; ++i) {
        sum += samples[i] * samples[i];
    }
    return std::sqrt(sum / static_cast<float>(end - start));
}

void UtteranceCollector::recompute_thresholds_() {
    const float floor = tracker_.floor();
    const bool auto_on = cfg_.auto_calibrate;

    if (auto_on && !cfg_.voice_rms_threshold_pinned) {
        dynamic_start_thr_ = std::clamp(cfg_.k_start * floor,
                                        cfg_.adaptive_min_start_thr,
                                        cfg_.adaptive_max_start_thr);
    } else {
        dynamic_start_thr_ = cfg_.voice_rms_threshold;
    }
    const float raw_continue_thr =
        std::max(0.0f, cfg_.k_continue) * dynamic_start_thr_;
    dynamic_continue_thr_ = raw_continue_thr;
    // Hard lower clamp so a floor that calibrated tiny doesn't leave
    // the continue threshold below room hiss (which would let the
    // recording state latch until the safety net fires).
    if (auto_on && cfg_.adaptive_min_continue_thr > 0.0f) {
        dynamic_continue_thr_ =
            std::max(dynamic_continue_thr_, cfg_.adaptive_min_continue_thr);
    }
    if (continue_clamp_cb_ && auto_on && cfg_.adaptive_min_continue_thr > 0.0f &&
        dynamic_continue_thr_ > raw_continue_thr + 1e-6f) {
        const auto now = std::chrono::steady_clock::now();
        const bool first =
            last_continue_clamp_cb_.time_since_epoch().count() == 0;
        if (first || now - last_continue_clamp_cb_ >= std::chrono::seconds(5)) {
            last_continue_clamp_cb_ = now;
            continue_clamp_cb_(raw_continue_thr, dynamic_continue_thr_,
                               dynamic_start_thr_);
        }
    }

    if (auto_on && !cfg_.min_utterance_rms_pinned) {
        dynamic_min_utt_rms_ = std::clamp(cfg_.k_utt * floor,
                                          cfg_.adaptive_min_utt_rms,
                                          cfg_.adaptive_max_utt_rms);
    } else {
        dynamic_min_utt_rms_ = cfg_.min_utterance_rms;
    }
}

void UtteranceCollector::update_floor_estimate_(float frame_rms,
                                                bool collecting) {
    if (!cfg_.auto_calibrate) return;
    // Freeze: calibrated + user disabled runtime adaptation.
    // External: speakers are actively bleeding into the mic — folding
    // those samples in would inflate the start threshold past speech.
    const bool freeze = tracker_.calibrated() && !cfg_.auto_adapt;
    const bool external = external_audio_active_.load(std::memory_order_acquire);
    if (freeze || external) return;
    tracker_.observe(frame_rms, collecting);
}

void UtteranceCollector::announce_calibration_once_() {
    if (!cfg_.auto_calibrate || !tracker_.calibrated() ||
        announced_calibration_done_) {
        return;
    }
    announced_calibration_done_ = true;
    std::cout << "🎯 Calibrated (floor=" << tracker_.floor()
              << " start=" << dynamic_start_thr_
              << " cont=" << dynamic_continue_thr_
              << " utt=" << dynamic_min_utt_rms_ << ")" << std::endl;
}

void UtteranceCollector::log_vad_debug_(float frame_rms, bool collecting) {
    if (!cfg_.debug || collecting) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - last_debug_log_ < std::chrono::seconds(1)) return;
    std::cerr << "[vad] floor=" << tracker_.floor()
              << " start=" << dynamic_start_thr_
              << " cont=" << dynamic_continue_thr_
              << " utt=" << dynamic_min_utt_rms_
              << " rms=" << frame_rms
              << (tracker_.calibrated() ? "" : " (calibrating)")
              << std::endl;
    last_debug_log_ = now;
}

bool UtteranceCollector::detect_voice_(float frame_rms, bool window_ready,
                                      bool collecting) const {
    if (!window_ready) return false;
    // Refuse to start a new utterance before calibration completes:
    // the seed-derived start threshold (clamped at adaptive_min_start_thr)
    // can otherwise trip on mic warm-up before a real noise floor exists,
    // and any speech that slipped in would also poison the median.
    // Once already collecting we never re-evaluate this gate, so an
    // in-progress utterance is never aborted by it.
    if (!collecting && cfg_.auto_calibrate && !tracker_.calibrated()) {
        return false;
    }
    float thr = collecting ? dynamic_continue_thr_ : dynamic_start_thr_;
    // While the assistant is speaking the mic captures the speaker
    // bleed; raise the bar so only real (louder-than-bleed) speech
    // crosses it and triggers ducking / TTS abort.  No effect when the
    // boost is 1.0.
    if (tts_active_.load(std::memory_order_acquire)) {
        thr *= std::max(1.0f, cfg_.tts_threshold_boost);
    }
    return frame_rms > thr;
}

void UtteranceCollector::advance_collection_(CollectedUtterance& u,
                                            bool has_voice) const {
    u.speech_ms += cfg_.poll_interval_ms;
    ++u.total_frames;
    if (has_voice) {
        ++u.voiced_frames;
        u.silence_ms = 0;
    } else {
        u.silence_ms += cfg_.poll_interval_ms;
    }
}

UtteranceCollector::EndReason
UtteranceCollector::end_reason_(const CollectedUtterance& u) const {
    // Silence-driven end takes precedence: if both could fire on the
    // same tick we want the natural close, not the safety net.
    if (u.speech_ms >= cfg_.min_speech_ms &&
        u.silence_ms >= cfg_.end_silence_ms) {
        return EndReason::Silence;
    }
    // Reactive early-close on a low recent voiced ratio:  the recording
    // is "open" because frame_rms is grazing the continue threshold,
    // but the voiced fraction inside the rolling window is too small
    // to be real speech.  Catches the noise-plume case before we hit
    // the much-larger `max_utterance_ms` safety net.
    if (cfg_.early_close_voiced_ratio > 0.0f &&
        recent_voiced_capacity_ > 0 &&
        u.speech_ms >= cfg_.early_close_min_speech_ms) {
        // `u.total_frames` is the number of ticks since the recording
        // started; the ring buffer's `assign(cap, 0)` pre-fills with
        // zeros so we can't trust `recent_voiced_.size()` as the fill
        // count.  Once we have at least `cap` real pushes the rolling
        // window is fully real samples; before that we use the partial
        // fill (capped at cap).
        const int filled =
            std::min(recent_voiced_capacity_, u.total_frames);
        if (filled >= recent_voiced_capacity_) {
            const float ratio =
                static_cast<float>(recent_voiced_sum_) /
                static_cast<float>(filled);
            if (ratio < cfg_.early_close_voiced_ratio) {
                return EndReason::EarlyLowActivity;
            }
        }
    }
    if (cfg_.max_utterance_ms > 0 && u.speech_ms >= cfg_.max_utterance_ms) {
        return EndReason::MaxDuration;
    }
    return EndReason::None;
}

void UtteranceCollector::reset_recent_voiced_window_() {
    if (cfg_.early_close_voiced_ratio <= 0.0f) {
        recent_voiced_.clear();
        recent_voiced_.shrink_to_fit();
        recent_voiced_head_ = 0;
        recent_voiced_sum_ = 0;
        recent_voiced_capacity_ = 0;
        return;
    }
    const int poll = std::max(1, cfg_.poll_interval_ms);
    const int window_ms = std::max(poll, cfg_.early_close_window_ms);
    const int cap = std::max(1, window_ms / poll);
    recent_voiced_capacity_ = cap;
    recent_voiced_.assign(static_cast<std::size_t>(cap), 0u);
    recent_voiced_head_ = 0;
    recent_voiced_sum_ = 0;
}

void UtteranceCollector::push_recent_voiced_(bool voiced) {
    if (cfg_.early_close_voiced_ratio <= 0.0f ||
        recent_voiced_capacity_ <= 0) {
        return;
    }
    const std::size_t cap = recent_voiced_.size();
    if (cap == 0) return;  // shouldn't happen, but defend.
    std::uint8_t& slot = recent_voiced_[recent_voiced_head_];
    recent_voiced_sum_ -= slot;
    slot = voiced ? 1u : 0u;
    recent_voiced_sum_ += slot;
    recent_voiced_head_ = (recent_voiced_head_ + 1) % cap;
}

void UtteranceCollector::announce_recording_complete_(EndReason reason) const {
    if (reason == EndReason::MaxDuration) {
        // Distinct line so the user (and the pipeline-event log) can
        // see that the safety net fired — usually a sign that
        // `continue_thr` is chasing ambient noise and the floor needs
        // to settle higher.
        std::cout << "⏹ Recording complete (max duration "
                  << cfg_.max_utterance_ms << " ms reached)" << std::endl;
    } else if (reason == EndReason::EarlyLowActivity) {
        // The reactive early-close path closed the recording because
        // the rolling voiced ratio fell below
        // `cfg_.early_close_voiced_ratio` — i.e. the recording was
        // tracking ambient noise rather than real speech.
        std::cout << "⏹ Recording complete (low recent activity)"
                  << std::endl;
    } else {
        std::cout << "⏹ Recording complete!" << std::endl;
    }
}

UtteranceCollector::TickResult UtteranceCollector::poll_tick_(
    CollectedUtterance& u, bool& collecting,
    std::vector<float>& vad_window) {
    capture_.snapshotRecent(static_cast<std::size_t>(cfg_.vad_window_samples),
                            vad_window);

    const bool window_ready =
        vad_window.size() >= static_cast<std::size_t>(cfg_.vad_window_samples);
    const float frame_rms = window_ready
                                ? rms(vad_window, 0, vad_window.size())
                                : 0.0f;

    if (window_ready) update_floor_estimate_(frame_rms, collecting);
    recompute_thresholds_();
    announce_calibration_once_();
    log_vad_debug_(frame_rms, collecting);

    const bool has_voice = detect_voice_(frame_rms, window_ready, collecting);

    // Notify subscribers (barge-in controller) on per-frame ON/OFF edges.
    // The atomic also publishes the current state so off-thread readers
    // don't have to subscribe.
    const bool prev_voice = voice_active_.load(std::memory_order_acquire);
    if (has_voice != prev_voice) {
        voice_active_.store(has_voice, std::memory_order_release);
        if (voice_state_cb_) voice_state_cb_(has_voice);
    }
    // Frame-cadence tick (every poll iteration, ~50 ms by default) so
    // deferred actions (e.g. ducking hold timer) fire even when no voice
    // edge happens this frame.
    if (frame_cb_) frame_cb_();

    if (has_voice && !collecting) {
        collecting = true;
        u = {};
        // Reset the rolling voiced-frame ring buffer so the new
        // utterance starts judging activity from scratch.
        reset_recent_voiced_window_();
        std::cout << "🔴 Recording..." << std::endl;
    }

    if (collecting) {
        push_recent_voiced_(has_voice);
        advance_collection_(u, has_voice);
        const EndReason reason = end_reason_(u);
        if (reason != EndReason::None) {
            announce_recording_complete_(reason);
            return {TickResult::Kind::EmitUtterance};
        }
    }

    return {TickResult::Kind::Continue};
}

std::optional<CollectedUtterance> UtteranceCollector::collect_next() {
    CollectedUtterance u;
    bool collecting = false;

    std::vector<float> vad_window;
    vad_window.reserve(static_cast<std::size_t>(cfg_.vad_window_samples));

    while (app_running_.load()) {
        if (poll_tick_(u, collecting, vad_window).kind ==
            TickResult::Kind::EmitUtterance) {
            // Snapshot the full buffer here (not in `poll_tick_`) so the
            // per-tick helper stays free of capture-buffer side effects
            // and is trivially observable from tests.
            u.pcm = capture_.snapshotBuffer();
            return u;
        }
        capture_.limitBufferSize(cfg_.buffer_max_seconds, cfg_.buffer_keep_seconds);
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.poll_interval_ms));
    }

    return std::nullopt;
}

} // namespace hecquin::voice
