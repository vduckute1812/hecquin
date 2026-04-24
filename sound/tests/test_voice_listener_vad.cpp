// Unit tests for VoiceListener's secondary VAD gate — the part that decides
// whether a freshly-collected utterance is loud and voiced enough to hand to
// Whisper.  The gate logic is extracted into a static pure function on
// VoiceListener, so we can drive it with synthetic stats and never touch
// SDL, Whisper, or the network.

#include "voice/VoiceListener.hpp"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

int fail(const char* message) {
    std::cerr << "[test_voice_listener_vad] FAIL: " << message << std::endl;
    return 1;
}

VoiceListenerConfig baseline() {
    VoiceListenerConfig cfg;
    // Explicit defaults — independent of any env overrides picked up
    // accidentally from the developer's shell.
    cfg.voice_rms_threshold    = 0.02f;
    cfg.min_voiced_frame_ratio = 0.30f;
    cfg.min_utterance_rms      = 0.015f;
    cfg.poll_interval_ms       = 50;
    return cfg;
}

float sine_rms(float amplitude) {
    // RMS of a pure tone = amplitude / sqrt(2); kept inline so the test reads
    // naturally without a numerical-analysis digression.
    return amplitude / std::sqrt(2.0f);
}

} // namespace

int main() {
    const auto cfg = baseline();

    // 1. Typical speech clip — 80% voiced frames, loud enough.  Should pass.
    {
        const auto d = VoiceListener::evaluate_secondary_gate(
            /*voiced_frames=*/40, /*effective_frames=*/50,
            /*mean_rms=*/sine_rms(0.10f), cfg);
        if (!d.accept)      return fail("loud, dense utterance should accept");
        if (d.too_quiet)    return fail("loud utterance flagged too_quiet");
        if (d.too_sparse)   return fail("dense utterance flagged too_sparse");
    }

    // 2. Whisper: mean_rms below min_utterance_rms even though most frames
    //    briefly crossed the per-frame threshold.  Should reject as too_quiet.
    {
        const auto d = VoiceListener::evaluate_secondary_gate(
            /*voiced_frames=*/35, /*effective_frames=*/50,
            /*mean_rms=*/0.010f, cfg);
        if (d.accept)       return fail("whisper-level audio should reject");
        if (!d.too_quiet)   return fail("whisper should flag too_quiet");
        if (d.too_sparse)   return fail("whisper should not flag too_sparse");
    }

    // 3. Brief background chatter: loud enough on average but only a few
    //    frames crossed the VAD threshold.  Should reject as too_sparse.
    {
        const auto d = VoiceListener::evaluate_secondary_gate(
            /*voiced_frames=*/5, /*effective_frames=*/50,
            /*mean_rms=*/sine_rms(0.08f), cfg);
        if (d.accept)       return fail("sparse utterance should reject");
        if (d.too_quiet)    return fail("sparse should not flag too_quiet");
        if (!d.too_sparse)  return fail("sparse should flag too_sparse");
    }

    // 4. Both knobs fail simultaneously — reason must surface both so
    //    operators tune whichever is most aggressive.
    {
        const auto d = VoiceListener::evaluate_secondary_gate(
            /*voiced_frames=*/3, /*effective_frames=*/50,
            /*mean_rms=*/0.005f, cfg);
        if (d.accept)       return fail("doubly-failing gate should reject");
        if (!d.too_quiet)   return fail("should flag too_quiet");
        if (!d.too_sparse)  return fail("should flag too_sparse");
    }

    // 5. Exact-threshold accept: voiced_ratio == threshold and mean_rms ==
    //    threshold should *not* reject (comparison is strict `<`).
    {
        const auto d = VoiceListener::evaluate_secondary_gate(
            /*voiced_frames=*/15, /*effective_frames=*/50,
            /*mean_rms=*/cfg.min_utterance_rms, cfg);
        if (!d.accept)      return fail("at-threshold audio should accept");
    }

    // 6. Short utterance ("yes") with a handful of frames.  effective_frames
    //    excludes the silence tail, so 3/4 voiced on a tiny clip passes.
    {
        const auto d = VoiceListener::evaluate_secondary_gate(
            /*voiced_frames=*/3, /*effective_frames=*/4,
            /*mean_rms=*/sine_rms(0.05f), cfg);
        if (!d.accept)      return fail("short loud utterance should accept");
    }

    // 7. Guard against a divide-by-zero on degenerate input (no frames);
    //    the gate must still behave deterministically.
    {
        const auto d = VoiceListener::evaluate_secondary_gate(
            /*voiced_frames=*/0, /*effective_frames=*/0,
            /*mean_rms=*/0.0f, cfg);
        if (d.accept)       return fail("zero-frame gate must reject");
    }

    // 8. Env overrides disable both knobs (set to zero) — anything passes.
    {
        auto permissive = cfg;
        permissive.min_voiced_frame_ratio = 0.0f;
        permissive.min_utterance_rms      = 0.0f;
        const auto d = VoiceListener::evaluate_secondary_gate(
            /*voiced_frames=*/0, /*effective_frames=*/50,
            /*mean_rms=*/0.0f, permissive);
        if (!d.accept)      return fail("disabled gate should accept anything");
    }

    std::cout << "[test_voice_listener_vad] OK" << std::endl;
    return 0;
}
