#include "tts/backend/PiperPipeBackend.hpp"

#include "tts/PiperSampleRate.hpp"
#include "tts/backend/PiperSpawn.hpp"
#include "tts/backend/PiperWaitStatus.hpp"

#include <iostream>

namespace hecquin::tts::backend {

bool PiperPipeBackend::synthesize(const std::string& text,
                                  const std::string& model_path,
                                  std::vector<std::int16_t>& samples_out,
                                  int& sample_rate_out) {
    std::vector<std::int16_t> pcm;
    pcm.reserve(hecquin::tts::kPiperSampleRate); // ~1 s of audio up front

    const PiperSpawnResult result = run_pipe_synth(
        text, model_path,
        [&](const std::int16_t* p, std::size_t n) {
            pcm.insert(pcm.end(), p, p + n);
            return true;
        });

    if (!result.spawned) {
        return false;
    }
    if (!log_piper_wait_status(result.exit_status)) {
        return false;
    }
    if (pcm.empty()) {
        std::cerr << "[piper] produced no audio" << std::endl;
        return false;
    }

    samples_out = std::move(pcm);
    sample_rate_out = hecquin::tts::kPiperSampleRate;
    return true;
}

} // namespace hecquin::tts::backend
