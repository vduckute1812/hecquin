#pragma once

namespace hecquin::tts::runtime {

/**
 * Prepare the environment required by the `piper` executable.
 *
 * On macOS this seeds `DYLD_FALLBACK_LIBRARY_PATH` / `DYLD_LIBRARY_PATH`
 * with espeak-ng + Homebrew + /usr/local paths so Piper's shared libs
 * resolve even when the binary is not launched through its wrapper
 * script.  The call is idempotent — setenv with overwrite=1 simply
 * rewrites the same string on repeat invocations.
 *
 * No-op on non-Apple platforms.
 */
void configure();

} // namespace hecquin::tts::runtime
