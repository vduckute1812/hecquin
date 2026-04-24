#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hecquin::tts::wav {

/**
 * Read a standard 44-byte-header PCM WAV file into mono int16 samples.
 *
 * The helper is intentionally minimal — it does not validate the `fmt`
 * chunk contents beyond the RIFF signature.  Callers that need anything
 * fancier (multi-channel, float, extended headers) should switch to a
 * real WAV library.
 *
 * Returns false on any I/O failure or invalid header.
 */
bool read_pcm_s16_mono(const std::string& filename, std::vector<int16_t>& samples_out);

/**
 * Parse the sample rate field (offset 24..27, little-endian uint32) from
 * a WAV file header.  Returns 0 if the file cannot be read or the header
 * is shorter than 44 bytes.
 */
int parse_sample_rate(const std::string& filename);

} // namespace hecquin::tts::wav
