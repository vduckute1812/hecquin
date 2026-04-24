#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Public facade over the TTS subsystem.  Implementation lives under
 * `src/tts/{backend,playback,runtime,wav}` — see ARCHITECTURE.md.
 *
 * These functions intentionally expose a flat C-style API so callers
 * outside `hecquin::tts` do not take a dependency on the Strategy /
 * Template-Method machinery underneath.  Implementation-level helpers
 * (the Piper backend interface, the SDL players, WAV I/O, runtime
 * configuration) are available under `tts/backend`, `tts/playback`,
 * `tts/wav` and `tts/runtime` respectively for tests and advanced
 * callers.
 */

/** Run Piper to write a WAV file (16-bit mono, typically 22050 Hz). */
bool piper_synthesize_wav(const std::string& text, const std::string& model_path, const std::string& output_wav_path);

/**
 * Synthesise `text` and return the resulting mono int16 samples plus the
 * sample rate parsed from the WAV header.  The temp file is removed before
 * returning.  Returns false on any failure.
 */
bool piper_synthesize_to_buffer(const std::string& text,
                                const std::string& model_path,
                                std::vector<int16_t>& samples_out,
                                int& sample_rate_out);

/**
 * Play mono int16 audio at Piper's sample rate via SDL default output device.
 * Does not call SDL_Quit. Safe if SDL audio was already initialized (e.g. capture running elsewhere).
 */
bool sdl_play_s16_mono_22k(const std::vector<int16_t>& samples);

/** Synthesize → load → play → delete temp file. Returns false if any step fails. */
bool piper_speak_and_play(const std::string& text, const std::string& model_path);

/**
 * Streaming variant: pipe Piper output directly into SDL playback as soon
 * as the first samples arrive.  Reduces perceived latency on long replies
 * (tutor corrections can be seconds long) by starting playback before
 * synthesis completes.  Falls back to the buffered path on pipe failure.
 */
bool piper_speak_and_play_streaming(const std::string& text,
                                    const std::string& model_path);
