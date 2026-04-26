#pragma once

namespace hecquin::tts::backend {

/**
 * Interpret the `waitpid`-style status returned by `PiperSpawnResult`
 * (or `std::system`) and emit the same `[piper] …` diagnostic that
 * both the streaming and buffered pipelines used to print inline.
 *
 * Returns `true` iff the process terminated normally with exit code 0
 * (i.e. caller should treat the synth as successful).  Returns `false`
 * for any signal exit, non-zero exit code, or other abnormal status —
 * the caller is expected to surface its own user-facing fallback in
 * that case.
 *
 * Single source of truth so the streaming and buffered pipelines drift
 * less easily on signal / exit-code formatting.
 */
bool log_piper_wait_status(int status);

} // namespace hecquin::tts::backend
