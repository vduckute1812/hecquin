#include "voice/TtsResponsePlayer.hpp"

#include "common/StringUtils.hpp"
#include "tts/PiperSpeech.hpp"
#include "voice/AudioCapture.hpp"

#include <iostream>
#include <regex>
#include <utility>

namespace hecquin::voice {

namespace {

// Markdown / formatting strippers compiled once — `std::regex` objects
// are expensive to build and we run this on every spoken reply.  The
// same static regexes are reused by every caller; `std::regex::search`
// is const so sharing is safe on all supported standard libraries.
const std::regex& rx_asterisks() {
    static const std::regex re(R"(\*{1,3})");
    return re;
}
const std::regex& rx_heading() {
    static const std::regex re(R"(^#{1,6}\s+)", std::regex_constants::multiline);
    return re;
}
const std::regex& rx_list_bullet() {
    static const std::regex re(R"(^[\-\*]\s+)", std::regex_constants::multiline);
    return re;
}
const std::regex& rx_list_numbered() {
    static const std::regex re(R"(^\d+\.\s+)", std::regex_constants::multiline);
    return re;
}
const std::regex& rx_inline_code() {
    static const std::regex re(R"(`([^`]+)`)");
    return re;
}
const std::regex& rx_whitespace_run() {
    static const std::regex re(R"([\r\n\t]+)");
    return re;
}
const std::regex& rx_double_space() {
    static const std::regex re(R"( {2,})");
    return re;
}

} // namespace

TtsResponsePlayer::TtsResponsePlayer(AudioCapture& capture, std::string piper_model_path)
    : capture_(capture), piper_model_path_(std::move(piper_model_path)) {}

std::string TtsResponsePlayer::sanitize(std::string s) {
    s = std::regex_replace(s, rx_asterisks(), "");
    s = std::regex_replace(s, rx_heading(), "");
    s = std::regex_replace(s, rx_list_bullet(), "");
    s = std::regex_replace(s, rx_list_numbered(), "");
    s = std::regex_replace(s, rx_inline_code(), "$1");
    s = std::regex_replace(s, rx_whitespace_run(), " ");
    s = std::regex_replace(s, rx_double_space(), " ");
    return hecquin::common::trim_copy(std::move(s));
}

void TtsResponsePlayer::speak(const Action& action, const char* mode_label) {
    std::cout << "🤖 Route: " << actionKindLabel(action.kind)
              << "  mode=" << mode_label << std::endl;
    if (action.reply.empty()) {
        return;
    }

    // MuteGuard pauses + clears mic on construction, clears + resumes on
    // exit.  Guarantees the mic is re-armed even if the TTS path throws.
    AudioCapture::MuteGuard mute(capture_);

    // Streaming playback: SDL starts as soon as Piper has synthesised
    // the first ~60 ms of audio, hiding the synth cost on long replies.
    if (!piper_speak_and_play_streaming(sanitize(action.reply), piper_model_path_)) {
        std::cerr << "🔇 TTS failed; reply text: " << action.reply << std::endl;
    }
}

} // namespace hecquin::voice
