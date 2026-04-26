#include "tts/backend/PiperShellBackend.hpp"

#include "common/ShellEscape.hpp"
#include "tts/PiperSampleRate.hpp"
#include "tts/runtime/PiperRuntime.hpp"
#include "tts/wav/WavReader.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/wait.h>

#ifndef PIPER_EXECUTABLE
#define PIPER_EXECUTABLE "piper"
#endif

namespace hecquin::tts::backend {

namespace {

constexpr const char* kTempWavFilename = "piper_output.wav";

using hecquin::common::posix_sh_single_quote;

std::string piper_temp_wav_path() {
    try {
        return (std::filesystem::temp_directory_path() / kTempWavFilename).string();
    } catch (...) {
        return std::string("/tmp/") + kTempWavFilename;
    }
}

} // namespace

bool PiperShellBackend::synthesize_to_wav(const std::string& text,
                                          const std::string& model_path,
                                          const std::string& output_wav_path) {
    if (!std::filesystem::exists(model_path)) {
        std::cerr << "Model not found: " << model_path << std::endl;
        std::cerr << "Run: ./dev.sh piper:download-model en_US-lessac-medium" << std::endl;
        return false;
    }

    runtime::configure();

    const std::string command =
        "echo " + posix_sh_single_quote(text) + " | " +
        posix_sh_single_quote(PIPER_EXECUTABLE) + " --model " +
        posix_sh_single_quote(model_path) + " --output_file " +
        posix_sh_single_quote(output_wav_path);

    std::cout << "🔊 Synthesizing speech..." << std::endl;

    const int result = std::system(command.c_str());
    if (result != 0) {
        if (result == -1) {
            std::cerr << "Failed to spawn process for Piper TTS" << std::endl;
        } else if (WIFSIGNALED(result)) {
            std::cerr << "Piper terminated by signal: " << WTERMSIG(result) << std::endl;
        } else if (WIFEXITED(result)) {
            std::cerr << "Piper TTS failed (exit code: " << WEXITSTATUS(result) << ")" << std::endl;
        } else {
            std::cerr << "Piper TTS failed (status: " << result << ")" << std::endl;
        }

        std::cerr << "Ensure Piper is installed: ./dev.sh piper:install" << std::endl;
        std::cerr << "On macOS, also install espeak-ng: brew install espeak-ng" << std::endl;
        return false;
    }

    return std::filesystem::exists(output_wav_path);
}

bool PiperShellBackend::synthesize(const std::string& text,
                                   const std::string& model_path,
                                   std::vector<std::int16_t>& samples_out,
                                   int& sample_rate_out) {
    const std::string temp_wav = piper_temp_wav_path();
    if (!synthesize_to_wav(text, model_path, temp_wav)) {
        return false;
    }
    int parsed_rate = hecquin::tts::wav::parse_sample_rate(temp_wav);
    std::vector<std::int16_t> samples;
    const bool ok = hecquin::tts::wav::read_pcm_s16_mono(temp_wav, samples);
    std::filesystem::remove(temp_wav);
    if (!ok) {
        return false;
    }
    samples_out = std::move(samples);
    sample_rate_out = parsed_rate > 0 ? parsed_rate : hecquin::tts::kPiperSampleRate;
    return true;
}

} // namespace hecquin::tts::backend
