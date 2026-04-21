#include "tts/PiperSpeech.hpp"

#include <SDL.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/wait.h>
#include <thread>

#ifndef DEFAULT_PIPER_MODEL_PATH
#define DEFAULT_PIPER_MODEL_PATH ".env/shared/models/piper/en_US-lessac-medium.onnx"
#endif

#ifndef PIPER_EXECUTABLE
#define PIPER_EXECUTABLE "piper"
#endif

namespace {

constexpr int kPiperSampleRate = 22050;
constexpr int kChannels = 1;
constexpr int kAudioBufferSamples = 4096;
constexpr size_t kWavHeaderSize = 44;
constexpr const char* kTempWavFilename = "piper_output.wav";

struct AudioState {
    std::vector<int16_t> samples;
    size_t position = 0;
    bool finished = false;
};

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

void configure_piper_runtime() {
#ifdef __APPLE__
    std::string fallback_libs;
    std::filesystem::path piper_path(PIPER_EXECUTABLE);

    if (piper_path.has_parent_path() && std::filesystem::exists(piper_path.parent_path())) {
        fallback_libs = piper_path.parent_path().string();
    }

    if (std::filesystem::exists("/opt/homebrew/opt/espeak-ng/lib")) {
        fallback_libs += (fallback_libs.empty() ? "" : ":") + std::string("/opt/homebrew/opt/espeak-ng/lib");
    }
    if (std::filesystem::exists("/opt/homebrew/lib")) {
        fallback_libs += (fallback_libs.empty() ? "" : ":") + std::string("/opt/homebrew/lib");
    }
    if (std::filesystem::exists("/usr/local/lib")) {
        fallback_libs += (fallback_libs.empty() ? "" : ":") + std::string("/usr/local/lib");
    }

    const char* current = std::getenv("DYLD_FALLBACK_LIBRARY_PATH");
    if (current != nullptr && *current != '\0') {
        fallback_libs += (fallback_libs.empty() ? "" : ":") + std::string(current);
    }

    if (!fallback_libs.empty()) {
        setenv("DYLD_FALLBACK_LIBRARY_PATH", fallback_libs.c_str(), 1);
        setenv("DYLD_LIBRARY_PATH", fallback_libs.c_str(), 1);
    }
#endif
}

void audio_playback_callback(void* userdata, Uint8* stream, int len) {
    auto* state = static_cast<AudioState*>(userdata);

    const int samples_to_copy = len / sizeof(int16_t);
    auto* out = reinterpret_cast<int16_t*>(stream);

    for (int i = 0; i < samples_to_copy; i++) {
        if (state->position < state->samples.size()) {
            out[i] = state->samples[state->position++];
        } else {
            out[i] = 0;
            state->finished = true;
        }
    }
}

bool open_playback_device(SDL_AudioDeviceID& dev, AudioState& state) {
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
            std::cerr << "Lỗi SDL_Init: " << SDL_GetError() << std::endl;
            return false;
        }
    }

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = kPiperSampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = kChannels;
    want.samples = static_cast<Uint16>(kAudioBufferSamples);
    want.callback = audio_playback_callback;
    want.userdata = &state;

    dev = SDL_OpenAudioDevice(nullptr, SDL_FALSE, &want, nullptr, 0);
    if (dev == 0) {
        std::cerr << "Lỗi mở audio device (playback): " << SDL_GetError() << std::endl;
        return false;
    }

    return true;
}

} // namespace

std::string piper_temp_wav_path() {
    try {
        return (std::filesystem::temp_directory_path() / kTempWavFilename).string();
    } catch (...) {
        return std::string("/tmp/") + kTempWavFilename;
    }
}

bool piper_synthesize_wav(const std::string& text, const std::string& model_path, const std::string& output_wav_path) {
    if (!std::filesystem::exists(model_path)) {
        std::cerr << "Model không tồn tại: " << model_path << std::endl;
        std::cerr << "Chạy: ./dev.sh piper:download-model en_US-lessac-medium" << std::endl;
        return false;
    }

    configure_piper_runtime();

    const std::string command = "echo " + shell_quote(text) + " | " + shell_quote(PIPER_EXECUTABLE) + " --model " +
                                shell_quote(model_path) + " --output_file " + shell_quote(output_wav_path);

    std::cout << "🔊 Đang tổng hợp giọng nói..." << std::endl;

    const int result = std::system(command.c_str());
    if (result != 0) {
        if (result == -1) {
            std::cerr << "Không thể tạo process cho Piper TTS" << std::endl;
        } else if (WIFSIGNALED(result)) {
            std::cerr << "Piper bị dừng bởi signal: " << WTERMSIG(result) << std::endl;
        } else if (WIFEXITED(result)) {
            std::cerr << "Lỗi chạy Piper TTS (exit code: " << WEXITSTATUS(result) << ")" << std::endl;
        } else {
            std::cerr << "Lỗi chạy Piper TTS (status: " << result << ")" << std::endl;
        }

        std::cerr << "Đảm bảo Piper đã được cài đặt: ./dev.sh piper:install" << std::endl;
        std::cerr << "Nếu dùng macOS, cài thêm espeak-ng: brew install espeak-ng" << std::endl;
        return false;
    }

    return std::filesystem::exists(output_wav_path);
}

bool wav_read_s16_mono(const std::string& filename, std::vector<int16_t>& samples) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Không thể mở file: " << filename << std::endl;
        return false;
    }

    char header[kWavHeaderSize];
    file.read(header, kWavHeaderSize);
    if (file.gcount() != static_cast<std::streamsize>(kWavHeaderSize)) {
        std::cerr << "File WAV không hợp lệ (header thiếu dữ liệu)" << std::endl;
        return false;
    }

    if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F') {
        std::cerr << "File không phải định dạng WAV" << std::endl;
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streampos end_pos = file.tellg();
    if (end_pos < static_cast<std::streampos>(kWavHeaderSize)) {
        std::cerr << "File WAV không hợp lệ (kích thước quá nhỏ)" << std::endl;
        return false;
    }
    const size_t file_size = static_cast<size_t>(end_pos);
    file.seekg(kWavHeaderSize, std::ios::beg);

    const size_t data_size = file_size - kWavHeaderSize;
    const size_t num_samples = data_size / sizeof(int16_t);

    samples.resize(num_samples);
    file.read(reinterpret_cast<char*>(samples.data()), static_cast<std::streamsize>(data_size));
    if (!file) {
        std::cerr << "Không thể đọc dữ liệu PCM từ file WAV" << std::endl;
        return false;
    }

    return true;
}

bool sdl_play_s16_mono_22k(const std::vector<int16_t>& samples) {
    AudioState state;
    state.samples = samples;
    state.position = 0;
    state.finished = false;

    SDL_AudioDeviceID audio_dev = 0;
    if (!open_playback_device(audio_dev, state)) {
        return false;
    }

    std::cout << "🔊 Đang phát giọng nói..." << std::endl;

    SDL_PauseAudioDevice(audio_dev, 0);

    while (!state.finished) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    SDL_CloseAudioDevice(audio_dev);
    return true;
}

bool piper_synthesize_to_buffer(const std::string& text,
                                const std::string& model_path,
                                std::vector<int16_t>& samples_out,
                                int& sample_rate_out) {
    samples_out.clear();
    sample_rate_out = 0;

    const std::string temp_wav = piper_temp_wav_path();
    if (!piper_synthesize_wav(text, model_path, temp_wav)) {
        return false;
    }

    // Parse sample rate from the WAV header before using the standard reader.
    {
        std::ifstream file(temp_wav, std::ios::binary);
        if (file) {
            char hdr[44];
            file.read(hdr, sizeof(hdr));
            if (file.gcount() == static_cast<std::streamsize>(sizeof(hdr))) {
                const auto u8 = [&](int i) {
                    return static_cast<uint32_t>(static_cast<unsigned char>(hdr[i]));
                };
                sample_rate_out = static_cast<int>(
                    u8(24) | (u8(25) << 8) | (u8(26) << 16) | (u8(27) << 24));
            }
        }
    }

    const bool ok = wav_read_s16_mono(temp_wav, samples_out);
    std::filesystem::remove(temp_wav);
    if (sample_rate_out <= 0) {
        sample_rate_out = kPiperSampleRate;
    }
    return ok;
}

bool piper_speak_and_play(const std::string& text, const std::string& model_path) {
    std::cout << "🗣️  Piper sẽ nói: " << text << std::endl ;
    const std::string temp_wav = piper_temp_wav_path();
    if (!piper_synthesize_wav(text, model_path, temp_wav)) {
        return false;
    }

    std::vector<int16_t> samples;
    if (!wav_read_s16_mono(temp_wav, samples)) {
        std::filesystem::remove(temp_wav);
        return false;
    }

    std::cout << "📊 Loaded " << samples.size() << " samples (" << static_cast<float>(samples.size()) / kPiperSampleRate
              << " s)" << std::endl;

    const bool ok = sdl_play_s16_mono_22k(samples);
    std::filesystem::remove(temp_wav);
    return ok;
}
