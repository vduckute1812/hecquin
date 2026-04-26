#include "voice/TtsResponsePlayer.hpp"

#include "common/StringUtils.hpp"
#include "tts/PiperSpeech.hpp"
#include "voice/AudioBargeInController.hpp"
#include "voice/AudioCapture.hpp"
#include "voice/UtteranceCollector.hpp"

#include <atomic>
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

TtsResponsePlayer::TtsResponsePlayer(AudioCapture& capture,
                                     std::string piper_model_path,
                                     AudioBargeInController* barge,
                                     UtteranceCollector* collector)
    : capture_(capture),
      piper_model_path_(std::move(piper_model_path)),
      barge_(barge),
      collector_(collector) {}

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

namespace {

/**
 * RAII helper: while alive, flips `collector->set_tts_active(true)` so
 * the per-frame VAD raises its thresholds (so the assistant's own
 * speaker bleed doesn't trip the barge-in detector) and resets it on
 * scope exit, even on exception or early return.  Null-safe.
 */
class TtsActiveGuard {
public:
    TtsActiveGuard(UtteranceCollector* collector,
                   AudioBargeInController* barge)
        : collector_(collector), barge_(barge) {
        if (collector_) collector_->set_tts_active(true);
        if (barge_)     barge_->set_tts_active(true);
    }
    ~TtsActiveGuard() {
        if (collector_) collector_->set_tts_active(false);
        if (barge_)     barge_->set_tts_active(false);
    }
    TtsActiveGuard(const TtsActiveGuard&) = delete;
    TtsActiveGuard& operator=(const TtsActiveGuard&) = delete;
private:
    UtteranceCollector* collector_;
    AudioBargeInController* barge_;
};

bool barge_in_live_mic(AudioBargeInController* barge) {
    return barge != nullptr && barge->tts_barge_in_enabled();
}

} // namespace

void TtsResponsePlayer::speak(const Action& action, const char* mode_label) {
    std::cout << "🤖 Route: " << actionKindLabel(action.kind)
              << "  mode=" << mode_label << std::endl;
    if (action.reply.empty()) {
        return;
    }

    const std::string text = sanitize(action.reply);
    if (barge_in_live_mic(barge_)) {
        speak_with_barge_in_(text, action);
    } else {
        speak_with_muted_mic_(text, action);
    }
}

void TtsResponsePlayer::speak_with_muted_mic_(const std::string& text,
                                              const Action& action) {
    AudioCapture::MuteGuard mute(capture_);
    if (!piper_speak_and_play_streaming(text, piper_model_path_)) {
        std::cerr << "🔇 TTS failed; reply text: " << action.reply << std::endl;
    }
}

void TtsResponsePlayer::speak_with_barge_in_(const std::string& text,
                                             const Action& action) {
    // 1. Raise the collector's per-frame VAD threshold for the duration
    //    so the speaker bleed alone doesn't keep flipping voice ON.
    // 2. Drop any frames already buffered (they were captured before
    //    the threshold rose and would otherwise pollute the next
    //    utterance).
    // 3. Wire the controller's abort fuse to a local atomic flag, then
    //    pass that flag into the streaming player so the read loop
    //    bails as soon as real voice is detected.
    TtsActiveGuard guard(collector_, barge_);
    capture_.clearBuffer();

    std::atomic<bool> abort_tts{false};
    barge_->set_tts_aborter([&abort_tts] {
        abort_tts.store(true, std::memory_order_release);
    });

    const bool ok = piper_speak_and_play_streaming(text, piper_model_path_, &abort_tts);

    // Detach the aborter before the stack-allocated atomic goes away.
    barge_->set_tts_aborter({});

    if (!ok && !abort_tts.load(std::memory_order_acquire)) {
        std::cerr << "🔇 TTS failed; reply text: " << action.reply << std::endl;
    } else if (abort_tts.load(std::memory_order_acquire)) {
        std::cout << "🔁 TTS aborted by incoming voice." << std::endl;
    }

    // Drop any residual mic frames captured during TTS so the next
    // collection starts clean.
    capture_.clearBuffer();
}

} // namespace hecquin::voice
