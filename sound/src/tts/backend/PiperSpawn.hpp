#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace hecquin::tts::backend {

/**
 * Outcome of running the shared Piper spawn/pipe loop.
 *
 * `spawned` is false if the child never started (pipe() or
 * posix_spawnp() failed); `exit_status` is the value produced by
 * waitpid() and is only meaningful when `spawned == true`.
 */
struct PiperSpawnResult {
    bool spawned = false;
    int exit_status = 0;
    std::string error_reason;
    bool ok_exit() const;
};

/**
 * Template Method — the shared skeleton every pipe-mode Piper backend
 * uses:
 *
 *   1. verify the model file exists
 *   2. configure the Piper runtime environment
 *   3. create two pipes, posix_spawnp `piper --model <...> --output_raw`
 *   4. write `text + "\n"` to the child stdin, then close it
 *   5. read int16 PCM chunks from the child stdout, forwarding each one
 *      to `on_samples`
 *   6. waitpid for the child, return a structured result
 *
 * The callback is invoked synchronously from the reader loop; returning
 * false from `on_samples` stops the read early (but the helper still
 * waitpids the child to avoid zombies).
 */
PiperSpawnResult run_pipe_synth(
    const std::string& text,
    const std::string& model_path,
    const std::function<bool(const std::int16_t* pcm, std::size_t n_samples)>& on_samples);

} // namespace hecquin::tts::backend
