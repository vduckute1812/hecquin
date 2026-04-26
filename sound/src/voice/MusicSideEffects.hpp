#pragma once

#include <functional>
#include <utility>

namespace hecquin::voice {

class AudioBargeInController;
class UtteranceCollector;

/**
 * Owns every side effect the listener needs to apply when a music
 * intent fires.  Extracted from `VoiceListener` to keep the loop's
 * mode-mutation code free of music plumbing and to provide a single
 * place where the speaker-bleed gate (`UtteranceCollector::set_external
 * _audio_active`) is toggled.
 *
 * The class deliberately holds raw, non-owning references to the
 * pieces it touches:
 *   - The three `std::function` callbacks (abort / pause / resume) are
 *     stored by value because they are inherently optional and can be
 *     swapped late.
 *   - The `UtteranceCollector*` is non-owning and may be `nullptr`
 *     until the listener has constructed it; every method tolerates a
 *     null collector so unit tests can drive it without one.
 */
class MusicSideEffects {
public:
    using AbortCallback  = std::function<void()>;
    using PauseCallback  = std::function<void()>;
    using ResumeCallback = std::function<void()>;

    MusicSideEffects() = default;

    void set_abort_callback(AbortCallback cb)   { abort_cb_  = std::move(cb); }
    void set_pause_callback(PauseCallback cb)   { pause_cb_  = std::move(cb); }
    void set_resume_callback(ResumeCallback cb) { resume_cb_ = std::move(cb); }
    void set_collector(UtteranceCollector* c)   { collector_ = c; }
    /**
     * Attach the listener's barge-in controller.  When set, the
     * `on_playback_*` hooks below also flip
     * `AudioBargeInController::set_music_active`, so the controller
     * knows whether to duck the SDL output on detected voice.
     * Optional: null is fine for tests / binaries without a mic.
     */
    void set_barge_controller(AudioBargeInController* c) { barge_ = c; }

    /**
     * Called when `MusicPlayback` fires (i.e. the async session has
     * dispatched a real song).  Engages the speaker-bleed gate so the
     * adaptive noise floor stops folding in mic-to-speaker leakage.
     */
    void on_playback_started();

    /**
     * Called when `MusicNotFound` fires (search miss / empty query).
     * Same mode exit as `on_playback_started` from the listener's
     * perspective, but no audio is playing so the gate stays
     * disengaged.  Kept as an explicit method to make the contract
     * obvious at the call site.
     */
    void on_playback_not_found();

    /**
     * Called when `MusicCancel` fires.  Aborts in-flight playback,
     * disengages the bleed gate, and force-recalibrates the noise
     * floor so post-stop ambient sound retunes the VAD immediately
     * (instead of waiting for the EMA to bleed off the inflated
     * estimate the previous song produced).
     */
    void on_cancel();

    /** Pause: forward to provider, disengage the bleed gate. */
    void on_pause();
    /** Resume: forward to provider, re-engage the bleed gate. */
    void on_resume();

private:
    /**
     * Toggle the two collaborators that need to know whether external
     * audio is playing, in lock-step.  `active=true` engages the
     * speaker-bleed gate and tells the barge controller music is live;
     * `active=false` does the inverse.  Both pointers are nullable.
     */
    void mark_external_audio_(bool active);

    AbortCallback  abort_cb_;
    PauseCallback  pause_cb_;
    ResumeCallback resume_cb_;
    UtteranceCollector* collector_ = nullptr;
    AudioBargeInController* barge_ = nullptr;
};

} // namespace hecquin::voice
