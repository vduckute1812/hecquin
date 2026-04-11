#include "voice/WhisperEngine.hpp"

#include "whisper.h"

#include <cstring>
#include <iostream>

WhisperEngine::WhisperEngine(const char* model_path) {
    std::cout << "Đang tải model Whisper..." << std::endl;
    whisper_context_params cparams = whisper_context_default_params();
    ctx_ = whisper_init_from_file_with_params(model_path, cparams);
    if (!ctx_) {
        std::cerr << "Lỗi: Không thể tải file model!" << std::endl;
        return;
    }
    std::cout << "Model loaded!" << std::endl;
}

WhisperEngine::~WhisperEngine() {
    if (ctx_) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }
}

std::string WhisperEngine::transcribe(const std::vector<float>& samples) {
    if (!ctx_ || samples.empty()) {
        return {};
    }

    std::cout << "🔍 Đang nhận diện..." << std::endl;

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress = false;
    wparams.print_special = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = false;
    wparams.language = "en";
    wparams.n_threads = 4;

    std::string joined;
    if (whisper_full(ctx_, wparams, samples.data(), static_cast<int>(samples.size())) == 0) {
        const int n_segments = whisper_full_n_segments(ctx_);
        std::cout << "📝 Kết quả:" << std::endl;
        for (int i = 0; i < n_segments; ++i) {
            const char* text = whisper_full_get_segment_text(ctx_, i);
            if (text && std::strlen(text) > 0) {
                std::cout << "  > " << text << std::endl;
                if (!joined.empty()) {
                    joined += ' ';
                }
                joined += text;
            }
        }
    }
    std::cout << std::endl;
    return joined;
}
