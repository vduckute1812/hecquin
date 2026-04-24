#include "tts/backend/PiperPipeBackend.hpp"

#include "tts/backend/PiperSpawn.hpp"

#include <iostream>
#include <sys/wait.h>

namespace hecquin::tts::backend {

namespace {

constexpr int kPiperSampleRate = 22050;

} // namespace

bool PiperPipeBackend::synthesize(const std::string& text,
                                  const std::string& model_path,
                                  std::vector<std::int16_t>& samples_out,
                                  int& sample_rate_out) {
    std::vector<std::int16_t> pcm;
    pcm.reserve(kPiperSampleRate); // ~1 s of audio up front

    const PiperSpawnResult result = run_pipe_synth(
        text, model_path,
        [&](const std::int16_t* p, std::size_t n) {
            pcm.insert(pcm.end(), p, p + n);
            return true;
        });

    if (!result.spawned) {
        return false;
    }
    if (!result.ok_exit()) {
        if (WIFSIGNALED(result.exit_status)) {
            std::cerr << "[piper] killed by signal " << WTERMSIG(result.exit_status) << std::endl;
        } else if (WIFEXITED(result.exit_status)) {
            std::cerr << "[piper] exited with " << WEXITSTATUS(result.exit_status) << std::endl;
        }
        return false;
    }
    if (pcm.empty()) {
        std::cerr << "[piper] produced no audio" << std::endl;
        return false;
    }

    samples_out = std::move(pcm);
    sample_rate_out = kPiperSampleRate;
    return true;
}

} // namespace hecquin::tts::backend
