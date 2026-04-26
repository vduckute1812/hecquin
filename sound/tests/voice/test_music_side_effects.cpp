// MusicSideEffects unit test.
//
// Validates that:
//   - Each on_* method dispatches to the right callback (and only it).
//   - Methods are no-ops when callbacks are unset (covers binaries that
//     do not surface music).
//   - A null collector pointer is tolerated by every method (covers the
//     pre-construction window inside VoiceListener).
//
// The collector-side gate (`set_external_audio_active` /
// `reset_noise_floor` semantics) is exercised separately by the
// noise-floor-tracker test + the integration paths in voice_listener_vad,
// so this test focuses on the dispatch contract.

#include "voice/MusicSideEffects.hpp"

#include "voice/AudioBargeInController.hpp"

#include <iostream>

namespace {

int failures = 0;

void expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "[FAIL] " << label << std::endl;
        ++failures;
    }
}

} // namespace

int main() {
    using hecquin::voice::MusicSideEffects;

    {
        MusicSideEffects fx;
        // No callbacks installed, no collector wired.  None of these
        // should crash or do anything observable.
        fx.on_playback_started();
        fx.on_playback_not_found();
        fx.on_cancel();
        fx.on_pause();
        fx.on_resume();
        expect(true, "no-op when nothing wired");
    }

    {
        int abort_calls = 0, pause_calls = 0, resume_calls = 0;
        MusicSideEffects fx;
        fx.set_abort_callback ([&]() { ++abort_calls; });
        fx.set_pause_callback ([&]() { ++pause_calls; });
        fx.set_resume_callback([&]() { ++resume_calls; });

        fx.on_playback_started();
        fx.on_playback_not_found();
        expect(abort_calls == 0 && pause_calls == 0 && resume_calls == 0,
               "playback_started / playback_not_found do not fire control callbacks");

        fx.on_cancel();
        expect(abort_calls == 1, "on_cancel fires abort callback");
        expect(pause_calls == 0 && resume_calls == 0,
               "on_cancel only fires abort");

        fx.on_pause();
        expect(pause_calls == 1, "on_pause fires pause callback");
        expect(abort_calls == 1 && resume_calls == 0,
               "on_pause only fires pause");

        fx.on_resume();
        expect(resume_calls == 1, "on_resume fires resume callback");
        expect(abort_calls == 1 && pause_calls == 1,
               "on_resume only fires resume");
    }

    {
        // Replace callbacks mid-life: subsequent dispatches must hit the
        // new closure, not the old one.  Catches accidental capture-by-ref
        // regressions.
        int first_aborts = 0, second_aborts = 0;
        MusicSideEffects fx;
        fx.set_abort_callback([&]() { ++first_aborts; });
        fx.on_cancel();
        fx.set_abort_callback([&]() { ++second_aborts; });
        fx.on_cancel();
        expect(first_aborts == 1 && second_aborts == 1,
               "swapping abort callback retargets dispatch");
    }

    {
        // Explicit nullptr collector: methods must remain safe.
        MusicSideEffects fx;
        fx.set_collector(nullptr);
        fx.on_playback_started();
        fx.on_cancel();
        fx.on_pause();
        fx.on_resume();
        expect(true, "null collector is tolerated");
    }

    {
        // T1.5 lock-step: every state-changing on_* method must move the
        // barge controller's music_active flag in lock-step with the
        // collector's external-audio gate.  The collector side has no
        // public observer, but the barge controller side is observable
        // via `ducking()` after a voice event (ducking only fires when
        // music is active), so we use it as a proxy.
        using hecquin::voice::AudioBargeInController;
        AudioBargeInController::Config cfg;
        cfg.attack_ms  = 0;   // synchronous-looking transitions for the test
        cfg.release_ms = 0;
        cfg.hold_ms    = 0;
        AudioBargeInController barge(cfg);

        MusicSideEffects fx;
        fx.set_barge_controller(&barge);

        fx.on_playback_started();
        barge.on_voice_state_change(true);
        expect(barge.ducking(),
               "playback_started -> barge.set_music_active(true) -> duck on voice");
        barge.on_voice_state_change(false);
        barge.tick(AudioBargeInController::Clock::now());
        expect(!barge.ducking(), "voice off + zero hold -> unduck");

        fx.on_pause();
        barge.on_voice_state_change(true);
        expect(!barge.ducking(),
               "on_pause -> music inactive -> no duck on voice");
        barge.on_voice_state_change(false);

        fx.on_resume();
        barge.on_voice_state_change(true);
        expect(barge.ducking(),
               "on_resume -> music active again -> duck on voice");
        barge.on_voice_state_change(false);
        barge.tick(AudioBargeInController::Clock::now());

        fx.on_cancel();
        barge.on_voice_state_change(true);
        expect(!barge.ducking(),
               "on_cancel -> music inactive -> no duck on voice");
    }

    if (failures == 0) {
        std::cout << "[test_music_side_effects] all assertions passed"
                  << std::endl;
        return 0;
    }
    return 1;
}
