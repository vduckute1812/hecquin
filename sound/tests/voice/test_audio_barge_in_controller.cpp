// AudioBargeInController unit test (no audio, no SDL).
//
// Drives the controller's voice-state edges + tick() and asserts the
// gain-setter / aborter sequencing:
//   - Voice ON during music ⇒ duck immediately at attack_ms.
//   - Voice OFF schedules a release after hold_ms; tick releases.
//   - Voice ON during TTS ⇒ aborter fires once per tts_active window.
//   - Music going inactive force-unducks even mid-hold.

#include "voice/AudioBargeInController.hpp"

#include <chrono>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "[FAIL] " << label << std::endl;
        ++failures;
    }
}

struct GainEvent {
    float linear;
    int   ramp_ms;
};

} // namespace

int main() {
    using hecquin::voice::AudioBargeInController;
    using Clock = AudioBargeInController::Clock;

    // Build with deterministic timings so we can assert exact ramp values.
    AudioBargeInController::Config cfg;
    cfg.music_duck_gain      = 0.25f;
    cfg.attack_ms            = 30;
    cfg.release_ms           = 250;
    cfg.hold_ms              = 200;
    cfg.tts_barge_in_enabled = true;
    cfg.tts_threshold_boost  = 2.5f;

    {
        AudioBargeInController ctl(cfg);
        std::vector<GainEvent> gains;
        int aborts = 0;
        ctl.set_music_gain_setter(
            [&](float g, int ms) { gains.push_back({g, ms}); });
        ctl.set_tts_aborter([&] { ++aborts; });

        // No music + no TTS: voice events do nothing.
        ctl.on_voice_state_change(true);
        ctl.on_voice_state_change(false);
        expect(gains.empty() && aborts == 0,
               "voice events ignored when neither music nor TTS active");

        // Music active: voice ON ducks; voice OFF schedules release;
        // tick before hold expiry leaves it ducked; tick after expiry releases.
        ctl.set_music_active(true);
        ctl.on_voice_state_change(true);
        expect(gains.size() == 1 && gains.back().linear == cfg.music_duck_gain &&
                   gains.back().ramp_ms == cfg.attack_ms,
               "voice ON ducks at attack timing");
        expect(ctl.ducking(), "ducking() reports true after duck");

        const auto t0 = Clock::now();
        ctl.on_voice_state_change(false);
        expect(gains.size() == 1, "voice OFF does not immediately unduck");

        ctl.tick(t0 + std::chrono::milliseconds(50));
        expect(gains.size() == 1, "tick before hold expiry stays ducked");

        ctl.tick(t0 + std::chrono::milliseconds(cfg.hold_ms + 10));
        expect(gains.size() == 2 && gains.back().linear == 1.0f &&
                   gains.back().ramp_ms == cfg.release_ms,
               "tick after hold releases at release timing");
        expect(!ctl.ducking(), "ducking() reports false after release");
    }

    {
        AudioBargeInController ctl(cfg);
        std::vector<GainEvent> gains;
        ctl.set_music_gain_setter(
            [&](float g, int ms) { gains.push_back({g, ms}); });
        ctl.set_music_active(true);

        // Idempotency: a stutter of voice ON edges only ducks once.
        ctl.on_voice_state_change(true);
        ctl.on_voice_state_change(true);
        expect(gains.size() == 1, "duplicate voice ON does not re-duck");

        // Music going inactive force-unducks immediately, regardless of hold.
        ctl.set_music_active(false);
        expect(gains.size() == 2 && gains.back().linear == 1.0f &&
                   gains.back().ramp_ms == 0,
               "music inactive force-unducks instantly");
    }

    {
        AudioBargeInController ctl(cfg);
        int aborts = 0;
        ctl.set_tts_aborter([&] { ++aborts; });

        // TTS active: voice ON fires the aborter once.
        ctl.set_tts_active(true);
        ctl.on_voice_state_change(true);
        expect(aborts == 1, "voice ON during TTS fires abort once");

        // Subsequent voice edges in the same TTS window do not re-fire.
        ctl.on_voice_state_change(false);
        ctl.on_voice_state_change(true);
        expect(aborts == 1, "abort fuse is one-shot per TTS window");

        // New TTS window resets the fuse.
        ctl.set_tts_active(false);
        ctl.set_tts_active(true);
        ctl.on_voice_state_change(false);
        ctl.on_voice_state_change(true);
        expect(aborts == 2, "fuse rearms when TTS active flips");
    }

    {
        // Disabled barge-in: voice during TTS does not abort.
        auto cfg2 = cfg;
        cfg2.tts_barge_in_enabled = false;
        AudioBargeInController ctl(cfg2);
        int aborts = 0;
        ctl.set_tts_aborter([&] { ++aborts; });
        ctl.set_tts_active(true);
        ctl.on_voice_state_change(true);
        expect(aborts == 0, "tts_barge_in_enabled=false suppresses abort");
    }

    {
        // tick() before any voice edge is a no-op.
        AudioBargeInController ctl(cfg);
        std::vector<GainEvent> gains;
        ctl.set_music_gain_setter(
            [&](float g, int ms) { gains.push_back({g, ms}); });
        ctl.set_music_active(true);
        ctl.tick(Clock::now() + std::chrono::seconds(10));
        expect(gains.empty(), "tick with no pending hold is a no-op");
    }

    if (failures == 0) {
        std::cout << "[test_audio_barge_in_controller] all assertions passed"
                  << std::endl;
        return 0;
    }
    return 1;
}
