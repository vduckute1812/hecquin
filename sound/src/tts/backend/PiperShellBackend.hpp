#pragma once

#include "tts/backend/IPiperBackend.hpp"

#include <string>

namespace hecquin::tts::backend {

/**
 * Legacy fallback backend: `echo <text> | piper --output_file <wav>` via
 * `std::system`, then re-read the WAV.  Kept for platforms where
 * `posix_spawn` is unavailable or Piper was installed without
 * `--output_raw` support.  Behaviour matches the original `piper_synthesize_wav`
 * path verbatim so existing callers see no change.
 */
class PiperShellBackend : public IPiperBackend {
public:
    bool synthesize(const std::string& text,
                    const std::string& model_path,
                    std::vector<std::int16_t>& samples_out,
                    int& sample_rate_out) override;

    /** Direct "synthesise to an arbitrary WAV path" — used by the
     *  TTS CLI which wants the WAV on disk rather than in memory. */
    static bool synthesize_to_wav(const std::string& text,
                                  const std::string& model_path,
                                  const std::string& output_wav_path);
};

} // namespace hecquin::tts::backend
