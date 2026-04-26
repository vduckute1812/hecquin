#include "voice/MusicSideEffects.hpp"

#include "voice/UtteranceCollector.hpp"

namespace hecquin::voice {

void MusicSideEffects::on_playback_started() {
    if (collector_) collector_->set_external_audio_active(true);
}

void MusicSideEffects::on_playback_not_found() {
    // Nothing to do: no audio is playing so the bleed gate stays off.
    // Method exists so the caller doesn't have to special-case the
    // miss outcome with a comment.
}

void MusicSideEffects::on_cancel() {
    if (abort_cb_) abort_cb_();
    if (collector_) {
        collector_->set_external_audio_active(false);
        // Reset rather than just disengaging: any speaker bleed that
        // sneaked through during the song already inflated the EMA,
        // and we don't want the next utterance to wait for the EMA
        // to bleed back down.  A clean recalibration window is faster
        // and more predictable.
        collector_->reset_noise_floor();
    }
}

void MusicSideEffects::on_pause() {
    if (pause_cb_) pause_cb_();
    if (collector_) collector_->set_external_audio_active(false);
}

void MusicSideEffects::on_resume() {
    if (resume_cb_) resume_cb_();
    if (collector_) collector_->set_external_audio_active(true);
}

} // namespace hecquin::voice
