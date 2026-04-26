#include "voice/WhisperEngine.hpp"

#include "common/EnvParse.hpp"
#include "voice/WhisperPostFilter.hpp"
#include "whisper.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
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

int auto_threads() {
    const unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) return 2;
    return static_cast<int>(std::max(2u, hw / 2u));
}

} // namespace

void WhisperConfig::apply_env_overrides() {
    namespace env = hecquin::common::env;
    if (const char* lang = env::read_string("HECQUIN_WHISPER_LANGUAGE")) {
        language = lang;
    }
    int i = 0;
    if (env::parse_int("HECQUIN_WHISPER_THREADS", i)   && i >= 0) n_threads = i;
    if (env::parse_int("HECQUIN_WHISPER_BEAM_SIZE", i) && i >= 0) beam_size = i;
    float f = 0.0f;
    if (env::parse_float("HECQUIN_WHISPER_NO_SPEECH", f) && f >= 0.0f && f <= 1.0f) {
        no_speech_prob_max = f;
    }
    std::size_t z = 0;
    if (env::parse_size("HECQUIN_WHISPER_MIN_ALNUM", z)) min_alnum_chars = z;
    bool flag = false;
    if (env::parse_bool("HECQUIN_WHISPER_SUPPRESS_SEGS", flag)) {
        suppress_segment_print = flag;
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

    auto accepted = hecquin::voice::WhisperPostFilter::filter(
        joined, worst_no_speech_prob, cfg_);
    if (!accepted) {
        return {};
    }
    return *accepted;
}
