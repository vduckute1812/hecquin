#include <SDL.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <chrono>
#include <sys/wait.h>

// Fallback nếu không được định nghĩa từ CMake
#ifndef DEFAULT_PIPER_MODEL_PATH
#define DEFAULT_PIPER_MODEL_PATH ".env/shared/models/piper/en_US-lessac-medium.onnx"
#endif

#ifndef PIPER_EXECUTABLE
#define PIPER_EXECUTABLE "piper"
#endif

// Cấu hình audio output (Piper TTS output 22050Hz mono)
constexpr int PIPER_SAMPLE_RATE = 22050;
constexpr int CHANNELS = 1;
constexpr int AUDIO_BUFFER_SAMPLES = 4096;
constexpr size_t WAV_HEADER_SIZE = 44;
constexpr const char* TEMP_WAV_FILENAME = "piper_output.wav";
constexpr const char* FALLBACK_TEXT = "Hello! I am your robot tutor. How can I help you today?";

std::string get_temp_wav_path() {
    try {
        return (std::filesystem::temp_directory_path() / TEMP_WAV_FILENAME).string();
    } catch (...) {
        return std::string("/tmp/") + TEMP_WAV_FILENAME;
    }
}

// Audio playback state
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

// Callback function cho SDL audio playback
void audio_playback_callback(void* userdata, Uint8* stream, int len) {
    AudioState* state = static_cast<AudioState*>(userdata);
    
    int samples_to_copy = len / sizeof(int16_t);
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    
    for (int i = 0; i < samples_to_copy; i++) {
        if (state->position < state->samples.size()) {
            out[i] = state->samples[state->position++];
        } else {
            out[i] = 0;
            state->finished = true;
        }
    }
}

bool init_sdl_audio_playback(SDL_AudioDeviceID& dev, AudioState& state) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cerr << "Lỗi SDL_Init: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = PIPER_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;  // Signed 16-bit
    want.channels = CHANNELS;
    want.samples = AUDIO_BUFFER_SAMPLES;
    want.callback = audio_playback_callback;
    want.userdata = &state;

    // Mở thiết bị playback (NULL = mặc định, SDL_FALSE = playback, not capture)
    dev = SDL_OpenAudioDevice(NULL, SDL_FALSE, &want, &have, 0);
    if (dev == 0) {
        std::cerr << "Lỗi mở audio device: " << SDL_GetError() << std::endl;
        return false;
    }

    std::cout << "Audio device: " << have.freq << "Hz, " 
              << (int)have.channels << " channels" << std::endl;

    return true;
}

// Đọc file WAV raw (PCM 16-bit)
bool read_wav_file(const std::string& filename, std::vector<int16_t>& samples) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Không thể mở file: " << filename << std::endl;
        return false;
    }

    // Đọc header WAV (44 bytes)
    char header[WAV_HEADER_SIZE];
    file.read(header, WAV_HEADER_SIZE);
    if (file.gcount() != static_cast<std::streamsize>(WAV_HEADER_SIZE)) {
        std::cerr << "File WAV không hợp lệ (header thiếu dữ liệu)" << std::endl;
        return false;
    }

    // Kiểm tra format
    if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F') {
        std::cerr << "File không phải định dạng WAV" << std::endl;
        return false;
    }

    // Đọc dữ liệu PCM
    file.seekg(0, std::ios::end);
    std::streampos end_pos = file.tellg();
    if (end_pos < static_cast<std::streampos>(WAV_HEADER_SIZE)) {
        std::cerr << "File WAV không hợp lệ (kích thước quá nhỏ)" << std::endl;
        return false;
    }
    size_t file_size = static_cast<size_t>(end_pos);
    file.seekg(WAV_HEADER_SIZE, std::ios::beg);

    size_t data_size = file_size - WAV_HEADER_SIZE;
    size_t num_samples = data_size / sizeof(int16_t);
    
    samples.resize(num_samples);
    file.read(reinterpret_cast<char*>(samples.data()), data_size);
    if (!file) {
        std::cerr << "Không thể đọc dữ liệu PCM từ file WAV" << std::endl;
        return false;
    }

    return true;
}

// Chạy Piper TTS để tổng hợp giọng nói
bool synthesize_speech(const std::string& text, const std::string& model_path, 
                       const std::string& output_file) {
    // Kiểm tra model tồn tại
    if (!std::filesystem::exists(model_path)) {
        std::cerr << "Model không tồn tại: " << model_path << std::endl;
        std::cerr << "Chạy: ./dev.sh piper:download để tải model" << std::endl;
        return false;
    }

    configure_piper_runtime();

    // Piper nhận input từ stdin và xuất WAV.
    std::string command = "echo " + shell_quote(text) + " | " +
                          shell_quote(PIPER_EXECUTABLE) + " --model " + shell_quote(model_path) +
                          " --output_file " + shell_quote(output_file);

    std::cout << "🔊 Đang tổng hợp giọng nói..." << std::endl;
    
    int result = std::system(command.c_str());
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

    return std::filesystem::exists(output_file);
}

// Phát audio từ vector samples
bool play_audio(const std::vector<int16_t>& samples) {
    AudioState state;
    state.samples = samples;
    state.position = 0;
    state.finished = false;

    SDL_AudioDeviceID audio_dev;
    if (!init_sdl_audio_playback(audio_dev, state)) {
        return false;
    }

    std::cout << "🔊 Đang phát giọng nói..." << std::endl;
    
    // Bắt đầu playback
    SDL_PauseAudioDevice(audio_dev, 0);
    
    // Chờ đến khi phát xong
    while (!state.finished) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Đợi thêm một chút để buffer được phát hết
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    SDL_CloseAudioDevice(audio_dev);
    return true;
}

void print_usage(const char* program) {
    std::cout << "Sử dụng: " << program << " [options] \"text to speak\"" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -m, --model <path>   Đường dẫn tới model Piper (.onnx)" << std::endl;
    std::cout << "  -o, --output <path>  Lưu audio ra file WAV (không phát)" << std::endl;
    std::cout << "  -h, --help           Hiển thị trợ giúp" << std::endl;
    std::cout << std::endl;
    std::cout << "Ví dụ:" << std::endl;
    std::cout << "  " << program << " \"Hello, I am your robot tutor!\"" << std::endl;
    std::cout << "  " << program << " -m custom_voice.onnx \"Hello world\"" << std::endl;
    std::cout << "  " << program << " -o output.wav \"Save this to file\"" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string model_path = DEFAULT_PIPER_MODEL_PATH;
    std::string output_file;
    std::string text;
    bool save_to_file = false;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else if ((arg == "-m" || arg == "--model") && i + 1 < argc) {
            model_path = argv[++i];
        }
        else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_file = argv[++i];
            save_to_file = true;
        }
        else if (arg[0] != '-') {
            text = arg;
        }
    }

    // Nếu không có text, đọc từ stdin hoặc dùng text mẫu
    if (text.empty()) {
        std::cout << "Nhập văn bản cần đọc (Enter để xác nhận):" << std::endl;
        std::cout << "> ";
        std::getline(std::cin, text);
        
        if (text.empty()) {
            text = FALLBACK_TEXT;
            std::cout << "Sử dụng text mẫu: \"" << text << "\"" << std::endl;
        }
    }

    std::cout << "📝 Text: \"" << text << "\"" << std::endl;
    std::cout << "🎯 Model: " << model_path << std::endl;

    // Tạo file output tạm thời
    std::string temp_wav = save_to_file ? output_file : get_temp_wav_path();

    // Tổng hợp giọng nói
    if (!synthesize_speech(text, model_path, temp_wav)) {
        return 1;
    }

    std::cout << "✅ Tổng hợp thành công!" << std::endl;

    if (save_to_file) {
        std::cout << "💾 Đã lưu audio vào: " << output_file << std::endl;
        return 0;
    }

    // Đọc file WAV
    std::vector<int16_t> samples;
    if (!read_wav_file(temp_wav, samples)) {
        return 1;
    }

    std::cout << "📊 Loaded " << samples.size() << " samples ("
              << (float)samples.size() / PIPER_SAMPLE_RATE << " giây)" << std::endl;

    // Phát audio
    if (!play_audio(samples)) {
        SDL_Quit();
        return 1;
    }

    std::cout << "✅ Hoàn tất!" << std::endl;

    // Xóa file tạm
    std::filesystem::remove(temp_wav);

    SDL_Quit();
    return 0;
}
