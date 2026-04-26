#pragma once

#include "voice/WhisperEngine.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace hecquin::voice {

/**
 * Pure post-filter applied to Whisper's joined segment text.
 *
 * Lifted out of `WhisperEngine::transcribe` so the gates (annotation
 * stripping, min-alnum threshold, max no-speech probability) can be
 * exercised without loading a GGML model file.  `transcribe` is now
 * just inference + segment-join + delegation here.
 *
 * Returns the cleaned transcript on accept, `nullopt` on any rejected
 * gate.  Rejection paths emit a single `🔇 …` warning to stderr so the
 * existing operator-facing logs stay byte-identical.
 */
struct WhisperPostFilter {
    /**
     * @param joined_text             concatenation of all segment texts
     *                                from one whisper_full() call.
     * @param worst_no_speech_prob    max per-segment no_speech_prob
     *                                from the same call.
     * @param cfg                     min-alnum / no-speech gates.
     */
    static std::optional<std::string>
    filter(std::string_view joined_text,
           float worst_no_speech_prob,
           const WhisperConfig& cfg);
};

} // namespace hecquin::voice
