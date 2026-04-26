#pragma once

#include "actions/Action.hpp"

#include <string>

class AudioCapture;

namespace hecquin::voice {

class AudioBargeInController;
class UtteranceCollector;

/**
 * Speaks assistant replies through Piper while keeping the mic muted.
 *
 * Encapsulates the reply-sanitiser (strips markdown / code fences /
 * whitespace artefacts so TTS pronunciation stays natural), the
 * `AudioCapture::MuteGuard` RAII dance, and the call into
 * `piper_speak_and_play_streaming`.
 *
 * Keeping this in its own TU gets the seven compiled regexes out of the
 * listener's hot path and gives the feature a testable seam that does
 * not require SDL / Piper at link time in tests (`sanitize` is pure).
 *
 * When an `AudioBargeInController*` is provided and its
 * `tts_barge_in_enabled()` is true, the mic stays live during TTS
 * (the `MuteGuard` is bypassed) and the controller registers a
 * one-shot abort flag with the streaming player so detected voice
 * cuts the assistant off mid-sentence.
 */
class TtsResponsePlayer {
public:
    TtsResponsePlayer(AudioCapture& capture,
                      std::string piper_model_path,
                      AudioBargeInController* barge = nullptr,
                      UtteranceCollector* collector = nullptr);

    /** Play the action's reply (no-op when reply is empty). */
    void speak(const Action& action, const char* mode_label);

    /** Pure helper exposed for tests. */
    static std::string sanitize(std::string s);

private:
    /**
     * Legacy (no-barge) Strategy: pause the mic with a `MuteGuard` for
     * the whole TTS so the assistant doesn't hear itself, then synth +
     * play.  No abort path; the assistant always finishes its reply.
     */
    void speak_with_muted_mic_(const std::string& text,
                               const Action& action);
    /**
     * Barge-in Strategy: keep the mic live, bump the collector's TTS
     * threshold via `TtsActiveGuard`, register a one-shot abort fuse
     * with the controller, and let `piper_speak_and_play_streaming`
     * cut off mid-sentence the moment voice is detected.
     */
    void speak_with_barge_in_(const std::string& text,
                              const Action& action);

    AudioCapture& capture_;
    std::string piper_model_path_;
    AudioBargeInController* barge_     = nullptr;
    /** Collector whose `set_tts_active` is flipped during speak() so
     *  the per-frame VAD raises its thresholds and the assistant's
     *  own bleed doesn't trip ducking / abort. */
    UtteranceCollector*     collector_ = nullptr;
};

} // namespace hecquin::voice
