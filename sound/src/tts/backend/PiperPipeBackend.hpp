#pragma once

#include "tts/backend/IPiperBackend.hpp"

namespace hecquin::tts::backend {

/**
 * Pipe-mode backend.  Spawns `piper --output_raw`, writes `text` on
 * stdin, and collects int16 PCM from stdout.  No shell, no temp file,
 * no quoting surface.
 *
 * Returns 22050 Hz for the lessac-medium convention — callers that need
 * a different rate should fall back through `PiperShellBackend` which
 * parses the model's WAV header.
 */
class PiperPipeBackend : public IPiperBackend {
public:
    bool synthesize(const std::string& text,
                    const std::string& model_path,
                    std::vector<std::int16_t>& samples_out,
                    int& sample_rate_out) override;
};

} // namespace hecquin::tts::backend
