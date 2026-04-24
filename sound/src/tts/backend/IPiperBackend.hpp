#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hecquin::tts::backend {

/**
 * Strategy interface — "synthesise `text` with `model_path` and hand
 * back a full int16 PCM buffer plus sample rate".  All TTS buffered
 * playback flows go through one of these.
 *
 * Implementations must leave `samples_out` untouched on failure (so
 * callers can daisy-chain fallbacks without needing to clear first).
 */
class IPiperBackend {
public:
    virtual ~IPiperBackend() = default;
    virtual bool synthesize(const std::string& text,
                            const std::string& model_path,
                            std::vector<std::int16_t>& samples_out,
                            int& sample_rate_out) = 0;
};

} // namespace hecquin::tts::backend
