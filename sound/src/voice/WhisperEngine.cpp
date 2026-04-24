#include "voice/WhisperEngine.hpp"

#include "whisper.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <regex>
#include <string>
#include <thread>

namespace detail {

void whisper_context_deleter(whisper_context* ctx) noexcept {
    if (ctx) {
        whisper_free(ctx);
    }
}

} // namespace detail

namespace {

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
std::string strip_nonspeech_annotations(const std::string& text) {
    static const std::regex re(R"(\[[^\]\[]*\]|\([^\)\(]*\))");
    return std::regex_replace(text, re, "");
}

std::string trim_copy(const std::string& s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    auto b = std::find_if(s.begin(), s.end(), not_space);
    auto e = std::find_if(s.rbegin(), s.rend(), not_space).base();
    return (b < e) ? std::string(b, e) : std::string{};
}

int auto_threads() {
    const unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) return 2;
    return static_cast<int>(std::max(2u, hw / 2u));
}

bool parse_int_env(const char* name, int& out) {
    const char* raw = std::getenv(name);
    if (!raw || *raw == '\0') return false;
    try {
        out = std::stoi(raw);
        return true;
    } catch (...) {
        std::cerr << "[whisper] ignoring invalid " << name << "=" << raw << std::endl;
        return false;
    }
}

bool parse_size_env(const char* name, std::size_t& out) {
    const char* raw = std::getenv(name);
    if (!raw || *raw == '\0') return false;
    try {
        const long long v = std::stoll(raw);
        if (v < 0) return false;
        out = static_cast<std::size_t>(v);
        return true;
    } catch (...) {
        std::cerr << "[whisper] ignoring invalid " << name << "=" << raw << std::endl;
        return false;
    }
}

bool parse_float_env(const char* name, float& out) {
    const char* raw = std::getenv(name);
    if (!raw || *raw == '\0') return false;
    try {
        out = std::stof(raw);
        return true;
    } catch (...) {
        std::cerr << "[whisper] ignoring invalid " << name << "=" << raw << std::endl;
        return false;
    }
}

} // namespace

void WhisperConfig::apply_env_overrides() {
    if (const char* lang = std::getenv("HECQUIN_WHISPER_LANGUAGE")) {
        if (*lang != '\0') language = lang;
    }
    int i = 0;
    if (parse_int_env("HECQUIN_WHISPER_THREADS", i) && i >= 0) n_threads = i;
    if (parse_int_env("HECQUIN_WHISPER_BEAM_SIZE", i) && i >= 0) beam_size = i;
    float f = 0.0f;
    if (parse_float_env("HECQUIN_WHISPER_NO_SPEECH", f) && f >= 0.0f && f <= 1.0f) {
        no_speech_prob_max = f;
    }
    std::size_t z = 0;
    if (parse_size_env("HECQUIN_WHISPER_MIN_ALNUM", z)) min_alnum_chars = z;
    if (const char* sup = std::getenv("HECQUIN_WHISPER_SUPPRESS_SEGS")) {
        suppress_segment_print = (std::string(sup) == "1" ||
                                  std::string(sup) == "true");
    }
}

WhisperEngine::WhisperEngine(const char* model_path, WhisperConfig cfg)
    : cfg_(std::move(cfg)) {
    if (cfg_.n_threads <= 0) {
        cfg_.n_threads = auto_threads();
    }

    std::cout << "Loading Whisper model... (threads=" << cfg_.n_threads
              << ", lang=" << cfg_.language
              << ", beam=" << cfg_.beam_size << ")" << std::endl;
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
    const auto t0 = std::chrono::steady_clock::now();

    const whisper_sampling_strategy strategy = (cfg_.beam_size > 0)
        ? WHISPER_SAMPLING_BEAM_SEARCH
        : WHISPER_SAMPLING_GREEDY;
    whisper_full_params wparams = whisper_full_default_params(strategy);
    wparams.print_progress   = false;
    wparams.print_special    = false;
    wparams.print_realtime   = false;
    wparams.print_timestamps = false;
    // Language: "auto" asks Whisper to detect.  Any other value is passed
    // through verbatim; unknown codes are silently coerced to English by
    // whisper.cpp so mistypes degrade gracefully.
    if (cfg_.language == "auto") {
        wparams.language = nullptr;
    } else {
        wparams.language = cfg_.language.c_str();
    }
    wparams.n_threads = cfg_.n_threads;
    if (cfg_.beam_size > 0) {
        wparams.beam_search.beam_size = cfg_.beam_size;
    }

    wparams.suppress_blank  = true;
    wparams.suppress_nst    = true;
    wparams.no_speech_thold = cfg_.no_speech_prob_max;
    wparams.logprob_thold   = -1.0f;

    std::string joined;
    float worst_no_speech_prob = 0.0f;
    if (whisper_full(ctx, wparams, samples.data(), static_cast<int>(samples.size())) == 0) {
        const int n_segments = whisper_full_n_segments(ctx);
        if (!cfg_.suppress_segment_print) {
            std::cout << "📝 Result:" << std::endl;
        }
        for (int i = 0; i < n_segments; ++i) {
            const char* text = whisper_full_get_segment_text(ctx, i);
            const float p_ns = whisper_full_get_segment_no_speech_prob(ctx, i);
            if (p_ns > worst_no_speech_prob) {
                worst_no_speech_prob = p_ns;
            }
            if (text && std::strlen(text) > 0) {
                if (!cfg_.suppress_segment_print) {
                    std::cout << "  > " << text
                              << "   (no_speech=" << p_ns << ")" << std::endl;
                }
                if (!joined.empty()) {
                    joined += ' ';
                }
                joined += text;
            }
        }
    }
    std::cout << std::endl;

    last_latency_ms_ = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count());
    last_no_speech_prob_ = worst_no_speech_prob;

    const std::string stripped = trim_copy(strip_nonspeech_annotations(joined));

    if (stripped.empty() || count_alnum(stripped) < cfg_.min_alnum_chars) {
        if (stripped != joined) {
            std::cerr << "🔇 Whisper output was only non-speech annotations, "
                         "dropping: '" << joined << "'" << std::endl;
        }
        return {};
    }

    if (worst_no_speech_prob > cfg_.no_speech_prob_max) {
        std::cerr << "🔇 High no_speech probability ("
                  << worst_no_speech_prob
                  << "), treating as noise: '" << stripped << "'" << std::endl;
        return {};
    }

    return stripped;
}
