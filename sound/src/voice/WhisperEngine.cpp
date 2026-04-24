#include "voice/WhisperEngine.hpp"

#include "whisper.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <regex>
#include <string>

namespace detail {

void whisper_context_deleter(whisper_context* ctx) noexcept {
    if (ctx) {
        whisper_free(ctx);
    }
}

} // namespace detail

namespace {

// Max per-segment no-speech probability we tolerate before treating the
// decoded output as Whisper hallucinating over silence / music / static.
// Whisper.cpp's internal default for `no_speech_thold` is 0.6f; we use the
// same cutoff on the returned probability so the check is consistent with
// what the decoder itself considers "no speech".
constexpr float kNoSpeechProbMax = 0.6f;

// Decoded text that is shorter than this (after trimming to alphanumerics) is
// almost always a hallucination triggered by a brief noise spike.
constexpr std::size_t kMinAlnumChars = 2;

std::size_t count_alnum(const std::string& s) {
    std::size_t n = 0;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            ++n;
        }
    }
    return n;
}

// Remove any bracketed non-speech annotation Whisper emits on noise/music.
// Examples this covers:
//   [MUSIC], [NOISE], [SOUND], [BLANK_AUDIO], [NO_SPEECH],
//   [Music playing], [inaudible], [silence],
//   (music), (applause), (laughter), (sighs)
// The filter is pattern-based so any future variant Whisper introduces is
// handled automatically — no hard-coded list to maintain.
std::string strip_nonspeech_annotations(const std::string& text) {
    // Remove `[...]` and `(...)` non-greedy. Multiple annotations per line
    // are all stripped.
    static const std::regex re(R"(\[[^\]\[]*\]|\([^\)\(]*\))");
    return std::regex_replace(text, re, "");
}

// Trim surrounding whitespace in-place.
std::string trim_copy(const std::string& s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    auto b = std::find_if(s.begin(), s.end(), not_space);
    auto e = std::find_if(s.rbegin(), s.rend(), not_space).base();
    return (b < e) ? std::string(b, e) : std::string{};
}

} // namespace

WhisperEngine::WhisperEngine(const char* model_path) {
    std::cout << "Loading Whisper model..." << std::endl;
    whisper_context_params cparams = whisper_context_default_params();
    whisper_context* raw = whisper_init_from_file_with_params(model_path, cparams);
    if (!raw) {
        std::cerr << "Error: Failed to load model file!" << std::endl;
        return;
    }
    ctx_.reset(raw);
    std::cout << "Model loaded!" << std::endl;
}

std::string WhisperEngine::transcribe(const std::vector<float>& samples) {
    whisper_context* ctx = ctx_.get();
    if (!ctx || samples.empty()) {
        return {};
    }

    std::cout << "🔍 Recognizing..." << std::endl;

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress = false;
    wparams.print_special = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = false;
    wparams.language = "en";
    wparams.n_threads = 4;

    // Bias the decoder against emitting noise-shaped output. `suppress_blank`
    // drops the blank token, `suppress_nst` drops non-speech tokens, and the
    // two thresholds let Whisper itself decide a segment is silence when its
    // average log-probability collapses or its no-speech probability spikes.
    wparams.suppress_blank  = true;
    wparams.suppress_nst    = true;
    wparams.no_speech_thold = kNoSpeechProbMax;
    wparams.logprob_thold   = -1.0f;

    std::string joined;
    float worst_no_speech_prob = 0.0f;
    if (whisper_full(ctx, wparams, samples.data(), static_cast<int>(samples.size())) == 0) {
        const int n_segments = whisper_full_n_segments(ctx);
        std::cout << "📝 Result:" << std::endl;
        for (int i = 0; i < n_segments; ++i) {
            const char* text = whisper_full_get_segment_text(ctx, i);
            const float p_ns = whisper_full_get_segment_no_speech_prob(ctx, i);
            if (p_ns > worst_no_speech_prob) {
                worst_no_speech_prob = p_ns;
            }
            if (text && std::strlen(text) > 0) {
                std::cout << "  > " << text
                          << "   (no_speech=" << p_ns << ")" << std::endl;
                if (!joined.empty()) {
                    joined += ' ';
                }
                joined += text;
            }
        }
    }
    std::cout << std::endl;

    // Strip every bracketed non-speech annotation Whisper produces — this
    // covers [MUSIC], [NOISE], [SOUND], [BLANK_AUDIO], [Music playing],
    // (applause), (laughter), (sighs), etc. without needing a hard-coded list.
    const std::string stripped = trim_copy(strip_nonspeech_annotations(joined));

    // If the decoder only emitted annotations (e.g. "[MUSIC]" or "[NOISE]
    // [NOISE]") the stripped text will be empty: no real speech was heard.
    if (stripped.empty() || count_alnum(stripped) < kMinAlnumChars) {
        if (stripped != joined) {
            std::cerr << "🔇 Whisper output was only non-speech annotations, "
                         "dropping: '" << joined << "'" << std::endl;
        }
        return {};
    }

    // Per-segment no-speech probability gate. Whisper returns one value per
    // segment; if *any* segment is mostly silence/noise, discarding the whole
    // utterance is safer than letting a hallucinated phrase through.
    if (worst_no_speech_prob > kNoSpeechProbMax) {
        std::cerr << "🔇 High no_speech probability ("
                  << worst_no_speech_prob
                  << "), treating as noise: '" << stripped << "'" << std::endl;
        return {};
    }

    return stripped;
}
