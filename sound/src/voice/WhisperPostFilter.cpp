#include "voice/WhisperPostFilter.hpp"

#include "common/StringUtils.hpp"

#include <cctype>
#include <iostream>
#include <regex>
#include <string>

namespace hecquin::voice {

namespace {

std::size_t count_alnum(const std::string& s) {
    std::size_t n = 0;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) ++n;
    }
    return n;
}

// Remove any bracketed non-speech annotation Whisper emits on noise
// or music. Examples this covers:
//   [MUSIC], [NOISE], [SOUND], [BLANK_AUDIO], [NO_SPEECH],
//   [Music playing], [inaudible], [silence],
//   (music), (applause), (laughter), (sighs)
std::string strip_nonspeech_annotations(const std::string& text) {
    static const std::regex re(R"(\[[^\]\[]*\]|\([^\)\(]*\))");
    return std::regex_replace(text, re, "");
}

} // namespace

std::optional<std::string>
WhisperPostFilter::filter(std::string_view joined_text,
                          float worst_no_speech_prob,
                          const WhisperConfig& cfg) {
    const std::string joined(joined_text);
    const std::string stripped =
        hecquin::common::trim_copy(strip_nonspeech_annotations(joined));

    if (stripped.empty() || count_alnum(stripped) < cfg.min_alnum_chars) {
        if (stripped != joined) {
            std::cerr << "🔇 Whisper output was only non-speech annotations, "
                         "dropping: '" << joined << "'" << std::endl;
        }
        return std::nullopt;
    }

    if (worst_no_speech_prob > cfg.no_speech_prob_max) {
        std::cerr << "🔇 High no_speech probability ("
                  << worst_no_speech_prob
                  << "), treating as noise: '" << stripped << "'" << std::endl;
        return std::nullopt;
    }

    return stripped;
}

} // namespace hecquin::voice
