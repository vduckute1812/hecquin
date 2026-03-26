#include "whisper.h"
#include <SDL.h>
#include <vector>
#include <string>
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

// Fallback nếu không được định nghĩa từ CMake
#ifndef DEFAULT_MODEL_PATH
#define DEFAULT_MODEL_PATH ".env/models/ggml-base.bin"
#endif

// Cấu hình audio (Whisper yêu cầu 16kHz, Mono)
constexpr int SAMPLE_RATE = 16000;
constexpr int CHANNELS = 1;
constexpr int RECORD_SECONDS = 5; // Thời gian ghi âm

// Buffer lưu dữ liệu audio
std::vector<float> g_pcmf32;
std::atomic<bool> g_recording{true};

// Callback function cho SDL audio capture
void audio_callback(void* userdata, Uint8* stream, int len) {
    if (!g_recording) return;
    
    // SDL capture với format AUDIO_F32 sẽ cho float trực tiếp
    float* samples = reinterpret_cast<float*>(stream);
    int n_samples = len / sizeof(float);
    
    // Thêm vào buffer
    g_pcmf32.insert(g_pcmf32.end(), samples, samples + n_samples);
}

bool init_sdl_audio(SDL_AudioDeviceID& dev) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cerr << "Lỗi SDL_Init: " << SDL_GetError() << std::endl;
        return false;
    }

    // Liệt kê các thiết bị capture
    int num_devices = SDL_GetNumAudioDevices(SDL_TRUE);
    std::cout << "Tìm thấy " << num_devices << " thiết bị ghi âm:" << std::endl;
    for (int i = 0; i < num_devices; i++) {
        std::cout << "  [" << i << "] " << SDL_GetAudioDeviceName(i, SDL_TRUE) << std::endl;
    }

    // Cấu hình audio capture
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_F32;  // Float 32-bit
    want.channels = CHANNELS;
    want.samples = 1024;      // Buffer size
    want.callback = audio_callback;

    // Mở thiết bị capture (NULL = mặc định)
    dev = SDL_OpenAudioDevice(NULL, SDL_TRUE, &want, &have, 0);
    if (dev == 0) {
        std::cerr << "Lỗi mở audio device: " << SDL_GetError() << std::endl;
        return false;
    }

    std::cout << "Audio device: " << have.freq << "Hz, " 
              << (int)have.channels << " channels, "
              << "format=" << have.format << std::endl;

    return true;
}

int main() {
    // 1. Khởi tạo ngữ cảnh Whisper
    std::cout << "Đang tải model Whisper..." << std::endl;
    struct whisper_context_params cparams = whisper_context_default_params();
    struct whisper_context * ctx = whisper_init_from_file_with_params(DEFAULT_MODEL_PATH, cparams);

    if (!ctx) {
        std::cerr << "Lỗi: Không thể tải file model!" << std::endl;
        return 1;
    }
    std::cout << "Model loaded!" << std::endl;

    // 2. Khởi tạo SDL Audio và ghi âm từ microphone
    SDL_AudioDeviceID audio_dev;
    if (!init_sdl_audio(audio_dev)) {
        whisper_free(ctx);
        return 1;
    }

    // Reserve buffer cho ~5 giây audio
    g_pcmf32.reserve(SAMPLE_RATE * RECORD_SECONDS);

    std::cout << "\n🎤 Bắt đầu ghi âm " << RECORD_SECONDS << " giây... Nói gì đó!" << std::endl;
    
    // Bắt đầu capture
    SDL_PauseAudioDevice(audio_dev, 0);
    
    // Chờ ghi âm
    std::this_thread::sleep_for(std::chrono::seconds(RECORD_SECONDS));
    
    // Dừng capture
    g_recording = false;
    SDL_PauseAudioDevice(audio_dev, 1);
    SDL_CloseAudioDevice(audio_dev);
    
    std::cout << "✅ Ghi âm xong! Đã thu " << g_pcmf32.size() << " samples ("
              << (float)g_pcmf32.size() / SAMPLE_RATE << " giây)" << std::endl;

    if (g_pcmf32.empty()) {
        std::cerr << "Lỗi: Không có dữ liệu audio!" << std::endl;
        whisper_free(ctx);
        SDL_Quit();
        return 1;
    }

    // 3. Cấu hình tham số nhận diện
    std::cout << "\n🔍 Đang nhận diện giọng nói..." << std::endl;
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress = false;
    wparams.print_special  = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = false;
    wparams.language       = "en"; // Ép định dạng tiếng Anh cho Robot Tutor
    wparams.n_threads      = 4;    // Tận dụng đa nhân

    // 4. Chạy nhận diện
    if (whisper_full(ctx, wparams, g_pcmf32.data(), g_pcmf32.size()) != 0) {
        std::cerr << "Lỗi xử lý âm thanh!" << std::endl;
        whisper_free(ctx);
        SDL_Quit();
        return 1;
    }

    // 5. Lấy kết quả văn bản
    std::cout << "\n📝 Kết quả:" << std::endl;
    const int n_segments = whisper_full_n_segments(ctx);
    for (int i = 0; i < n_segments; ++i) {
        const char * text = whisper_full_get_segment_text(ctx, i);
        std::cout << "Robot heard: " << text << std::endl;
    }

    whisper_free(ctx);
    SDL_Quit();
    return 0;
}