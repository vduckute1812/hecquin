#pragma once

#include <cstdint>
#include <string>
#include <vector>

/** Run Piper to write a WAV file (16-bit mono, typically 22050 Hz). */
bool piper_synthesize_wav(const std::string& text, const std::string& model_path, const std::string& output_wav_path);

/** Read a standard PCM WAV (44-byte header) into interleaved or mono int16 samples. */
bool wav_read_s16_mono(const std::string& filename, std::vector<int16_t>& samples_out);

/**
 * Play mono int16 audio at Piper’s sample rate via SDL default output device.
 * Does not call SDL_Quit. Safe if SDL audio was already initialized (e.g. capture running elsewhere).
 */
bool sdl_play_s16_mono_22k(const std::vector<int16_t>& samples);

/** Temp WAV path under the system temp directory (unique name not required for single-threaded use). */
std::string piper_temp_wav_path();

/** Synthesize → load → play → delete temp file. Returns false if any step fails. */
bool piper_speak_and_play(const std::string& text, const std::string& model_path);
