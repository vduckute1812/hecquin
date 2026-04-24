#include "tts/backend/PiperFallbackBackend.hpp"

#include "tts/backend/PiperPipeBackend.hpp"
#include "tts/backend/PiperShellBackend.hpp"

#include <utility>

namespace hecquin::tts::backend {

PiperFallbackBackend::PiperFallbackBackend(std::unique_ptr<IPiperBackend> primary,
                                           std::unique_ptr<IPiperBackend> fallback)
    : primary_(std::move(primary)), fallback_(std::move(fallback)) {}

bool PiperFallbackBackend::synthesize(const std::string& text,
                                      const std::string& model_path,
                                      std::vector<std::int16_t>& samples_out,
                                      int& sample_rate_out) {
    samples_out.clear();
    sample_rate_out = 0;
    if (primary_ && primary_->synthesize(text, model_path, samples_out, sample_rate_out)) {
        return true;
    }
    samples_out.clear();
    sample_rate_out = 0;
    if (fallback_ && fallback_->synthesize(text, model_path, samples_out, sample_rate_out)) {
        return true;
    }
    return false;
}

std::unique_ptr<IPiperBackend> make_default_backend() {
    return std::make_unique<PiperFallbackBackend>(
        std::make_unique<PiperPipeBackend>(),
        std::make_unique<PiperShellBackend>());
}

} // namespace hecquin::tts::backend
