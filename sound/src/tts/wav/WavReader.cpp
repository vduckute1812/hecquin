#include "tts/wav/WavReader.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>

namespace hecquin::tts::wav {

namespace {

constexpr std::size_t kWavHeaderSize = 44;

} // namespace

bool read_pcm_s16_mono(const std::string& filename, std::vector<int16_t>& samples) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }

    char header[kWavHeaderSize];
    file.read(header, kWavHeaderSize);
    if (file.gcount() != static_cast<std::streamsize>(kWavHeaderSize)) {
        std::cerr << "Invalid WAV file (header is missing data)" << std::endl;
        return false;
    }

    if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F') {
        std::cerr << "File is not in WAV format" << std::endl;
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streampos end_pos = file.tellg();
    if (end_pos < static_cast<std::streampos>(kWavHeaderSize)) {
        std::cerr << "Invalid WAV file (size is too small)" << std::endl;
        return false;
    }
    const std::size_t file_size = static_cast<std::size_t>(end_pos);
    file.seekg(kWavHeaderSize, std::ios::beg);

    const std::size_t data_size = file_size - kWavHeaderSize;
    const std::size_t num_samples = data_size / sizeof(int16_t);

    samples.resize(num_samples);
    file.read(reinterpret_cast<char*>(samples.data()), static_cast<std::streamsize>(data_size));
    if (!file) {
        std::cerr << "Failed to read PCM data from WAV file" << std::endl;
        return false;
    }

    return true;
}

int parse_sample_rate(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return 0;
    char hdr[kWavHeaderSize];
    file.read(hdr, sizeof(hdr));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(hdr))) return 0;
    const auto u8 = [&](int i) {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(hdr[i]));
    };
    return static_cast<int>(u8(24) | (u8(25) << 8) | (u8(26) << 16) | (u8(27) << 24));
}

} // namespace hecquin::tts::wav
