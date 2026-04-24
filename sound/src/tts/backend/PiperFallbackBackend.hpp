#pragma once

#include "tts/backend/IPiperBackend.hpp"

#include <memory>

namespace hecquin::tts::backend {

/**
 * Composite Strategy: try `primary` first, fall back to `fallback` if
 * primary reports failure.  Keeps the fallback decision out of the
 * calling code so the facade stays thin.
 */
class PiperFallbackBackend : public IPiperBackend {
public:
    PiperFallbackBackend(std::unique_ptr<IPiperBackend> primary,
                         std::unique_ptr<IPiperBackend> fallback);

    bool synthesize(const std::string& text,
                    const std::string& model_path,
                    std::vector<std::int16_t>& samples_out,
                    int& sample_rate_out) override;

private:
    std::unique_ptr<IPiperBackend> primary_;
    std::unique_ptr<IPiperBackend> fallback_;
};

/** Convenience factory for the default `Pipe → Shell` fallback chain. */
std::unique_ptr<IPiperBackend> make_default_backend();

} // namespace hecquin::tts::backend
