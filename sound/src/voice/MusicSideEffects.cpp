#include "voice/MusicSideEffects.hpp"

#include "common/EnvParse.hpp"
#include "voice/AudioBargeInController.hpp"
#include "voice/UtteranceCollector.hpp"

namespace hecquin::voice {

void MusicSideEffects::mark_external_audio_(bool active) {
    if (collector_) collector_->set_external_audio_active(active);
    if (barge_)     barge_->set_music_active(active);
}

void MusicSideEffects::on_playback_started() {
    mark_external_audio_(true);
}

void MusicSideEffects::on_playback_not_found() {
    // Nothing to do: no audio is playing so the bleed gate stays off.
    // Method exists so the caller doesn't have to special-case the
    // miss outcome with a comment.
}

void MusicSideEffects::on_cancel() {
    // Tier-3 #12: confirm-cancel.  When opted in, the first cancel
    // arms a short confirmation window and just ducks the music
    // instead of tearing it down.  A second cancel inside the window
    // proceeds with the actual abort.  Outside the window the duck is
    // released and the cancel re-arms the confirm rather than fires.
    if (confirm_enabled_) {
        const auto now = std::chrono::steady_clock::now();
        const bool inside_window = confirm_pending_ &&
            (now - confirm_armed_at_) < confirm_window_;
        if (!inside_window) {
            confirm_pending_ = true;
            confirm_armed_at_ = now;
            // Soft duck so the user can still hear the song fade
            // while the assistant asks for confirmation.  No abort
            // yet — control returns to the listener which speaks the
            // reply.
            if (barge_) {
                barge_->tts_speak_begin(confirm_duck_gain_, 80);
            }
            return;
        }
        confirm_pending_ = false;
        if (barge_) {
            barge_->tts_speak_end(80);
        }
    }

    if (abort_cb_) abort_cb_();
    mark_external_audio_(false);
    // Reset rather than just disengaging: any speaker bleed that
    // sneaked through during the song already inflated the EMA, and
    // we don't want the next utterance to wait for the EMA to bleed
    // back down.  A clean recalibration window is faster and more
    // predictable.
    if (collector_) collector_->reset_noise_floor();
}

void MusicSideEffects::set_confirm_cancel(bool on,
                                          std::chrono::milliseconds window,
                                          float confirm_duck_gain) {
    confirm_enabled_ = on;
    confirm_window_ = window;
    confirm_duck_gain_ = confirm_duck_gain;
    confirm_pending_ = false;
}

void MusicSideEffects::apply_env_overrides() {
    namespace env = hecquin::common::env;
    bool flag = false;
    if (env::parse_bool("HECQUIN_CONFIRM_CANCEL", flag)) {
        confirm_enabled_ = flag;
    }
    int ms = 0;
    if (env::parse_int("HECQUIN_CONFIRM_CANCEL_MS", ms)) {
        confirm_window_ = std::chrono::milliseconds(ms);
    }
}

void MusicSideEffects::on_pause() {
    if (pause_cb_) pause_cb_();
    mark_external_audio_(false);
}

void MusicSideEffects::on_resume() {
    if (resume_cb_) resume_cb_();
    mark_external_audio_(true);
}

void MusicSideEffects::on_volume_up() {
    if (volume_cb_) volume_cb_(+1);
}

void MusicSideEffects::on_volume_down() {
    if (volume_cb_) volume_cb_(-1);
}

void MusicSideEffects::on_skip() {
    if (skip_cb_) skip_cb_();
}

void MusicSideEffects::set_tts_duck_gain(float linear, int ramp_ms) {
    tts_duck_gain_ = linear;
    tts_duck_ramp_ms_ = ramp_ms;
}

void MusicSideEffects::on_tts_speak_begin() {
    if (tts_speaking_) return;
    tts_speaking_ = true;
    // The barge controller already exposes a music gain setter; reuse
    // it so we don't accumulate yet another path through the audio
    // pipeline.  No-op if no music is playing or no setter is wired.
    if (barge_) {
        // We cannot reach the gain setter directly without exposing
        // more API; instead, leverage `set_music_active` semantics:
        // dispatching a duck happens via `emit_gain_` which is private,
        // so we go through the controller's voice-state path.  The
        // safer abstraction lives in `AudioBargeInController` below.
        // For now we call `set_music_active(true)` only when
        // appropriate — actual gain ramp is handled by the controller
        // through the new `tts_speak_begin/end` methods.
        barge_->tts_speak_begin(tts_duck_gain_, tts_duck_ramp_ms_);
    }
}

void MusicSideEffects::on_tts_speak_end() {
    if (!tts_speaking_) return;
    tts_speaking_ = false;
    if (barge_) {
        barge_->tts_speak_end(tts_duck_ramp_ms_);
    }
}

} // namespace hecquin::voice
