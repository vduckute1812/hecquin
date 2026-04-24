#pragma once

#include "actions/Action.hpp"

#include <string>

class AudioCapture;

namespace hecquin::voice {

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
 */
class TtsResponsePlayer {
public:
    TtsResponsePlayer(AudioCapture& capture, std::string piper_model_path);

    /** Play the action's reply (no-op when reply is empty). */
    void speak(const Action& action, const char* mode_label);

    /** Pure helper exposed for tests. */
    static std::string sanitize(std::string s);

private:
    AudioCapture& capture_;
    std::string piper_model_path_;
};

} // namespace hecquin::voice
