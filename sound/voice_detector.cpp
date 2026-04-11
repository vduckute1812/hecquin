#include "whisper.h"
#include "CommandProcessor.hpp"

#include <SDL.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <future>
#include <thread>
#include <vector>

// Fallback nếu không được định nghĩa từ CMake
#ifndef DEFAULT_MODEL_PATH
#define DEFAULT_MODEL_PATH ".env/models/ggml-base.bin"
#endif

// Cấu hình audio (Whisper yêu cầu 16kHz, Mono)
constexpr int SAMPLE_RATE = 16000;
constexpr int CHANNELS = 1;
constexpr int VAD_BUFFER_SIZE = 512;          // Kích thước buffer VAD
constexpr int MIN_SPEECH_DURATION = 500;      // Thời gian tối thiểu để kích hoạt (ms)
constexpr int SILENCE_DURATION = 800;         // Thời gian im lặng trước khi dừng (ms)
constexpr float VOICE_THRESHOLD = 0.02f;      // Ngưỡng năng lượng phát hiện giọng nói

// Buffer lưu dữ liệu audio có thể truy cập từ callback
std::vector<float> g_audio_buffer;
std::atomic<bool> g_listening{true};

// Signal handler cho Ctrl+C
void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n⏹ Nhận tín hiệu dừng, đang thoát..." << std::endl;
        g_listening = false;
    }
}

// Callback function cho SDL audio capture
void audio_callback(void* userdata, Uint8* stream, int len) {
    (void)userdata; // Unused parameter
    if (!g_listening) return;
    
    // SDL capture với format AUDIO_F32 sẽ cho float trực tiếp
    float* samples = reinterpret_cast<float*>(stream);
    int n_samples = len / sizeof(float);
    
    // Thêm vào buffer
    g_audio_buffer.insert(g_audio_buffer.end(), samples, samples + n_samples);
}

// Tính năng lượng RMS của audio để phát hiện giọng nói
float compute_rms(const std::vector<float>& samples, size_t start, size_t end) {
    if (start >= end || end > samples.size()) return 0.0f;
    
    float sum = 0.0f;
    for (size_t i = start; i < end; i++) {
        sum += samples[i] * samples[i];
    }
    return std::sqrt(sum / (end - start));
}

// Kiểm tra xem có giọng nói trong audio buffer không
bool has_voice_activity(const std::vector<float>& samples, size_t window_size = VAD_BUFFER_SIZE) {
    if (samples.size() < window_size) return false;
    
    float rms = compute_rms(samples, samples.size() - window_size, samples.size());
    return rms > VOICE_THRESHOLD;
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

// Khởi tạo Whisper model
struct whisper_context* init_whisper() {
    std::cout << "Đang tải model Whisper..." << std::endl;
    struct whisper_context_params cparams = whisper_context_default_params();
    struct whisper_context* ctx = whisper_init_from_file_with_params(DEFAULT_MODEL_PATH, cparams);

    if (!ctx) {
        std::cerr << "Lỗi: Không thể tải file model!" << std::endl;
        return nullptr;
    }
    std::cout << "Model loaded!" << std::endl;
    return ctx;
}

// Whisper STT — returns joined transcript text (may be empty).
std::string transcribe_speech(struct whisper_context* ctx, const std::vector<float>& speech_buffer) {
    if (speech_buffer.empty()) {
        return {};
    }

    std::cout << "🔍 Đang nhận diện..." << std::endl;

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress = false;
    wparams.print_special = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = false;
    wparams.language = "en";
    wparams.n_threads = 4;

    std::string joined;
    if (whisper_full(ctx, wparams, speech_buffer.data(), speech_buffer.size()) == 0) {
        const int n_segments = whisper_full_n_segments(ctx);
        std::cout << "📝 Kết quả:" << std::endl;
        for (int i = 0; i < n_segments; ++i) {
            const char* text = whisper_full_get_segment_text(ctx, i);
            if (text && std::strlen(text) > 0) {
                std::cout << "  > " << text << std::endl;
                if (!joined.empty()) {
                    joined += ' ';
                }
                joined += text;
            }
        }
    }
    std::cout << std::endl;
    return joined;
}

void print_action(const Action& a) {
    std::cout << "🤖 Route: ";
    switch (a.kind) {
        case ActionKind::None:
            std::cout << "None";
            break;
        case ActionKind::LocalDevice:
            std::cout << "LocalDevice";
            break;
        case ActionKind::InteractionTopicSearch:
            std::cout << "TopicSearch";
            break;
        case ActionKind::InteractionMusicSearch:
            std::cout << "MusicSearch";
            break;
        case ActionKind::ExternalApi:
            std::cout << "ExternalApi";
            break;
        case ActionKind::AssistantSdk:
            std::cout << "AssistantSdk";
            break;
    }
    std::cout << "\n💬 " << a.reply << "\n\n";
}

// Giới hạn kích thước buffer để tiết kiệm memory
void limit_buffer_size(std::vector<float>& buffer, int max_seconds = 30, int keep_seconds = 10) {
    size_t max_samples = SAMPLE_RATE * max_seconds;
    if (buffer.size() > max_samples) {
        size_t keep_samples = SAMPLE_RATE * keep_seconds;
        buffer.erase(buffer.begin(), buffer.begin() + (buffer.size() - keep_samples));
    }
}

// Xử lý vòng lặp lắng nghe chính
void listening_loop(struct whisper_context* ctx, SDL_AudioDeviceID audio_dev, CommandProcessor& commands) {
    int silence_timeout_ms = 0;
    bool collecting_speech = false;
    int speech_duration_ms = 0;
    std::vector<float> speech_buffer;

    std::cout << "\n🎤 Đang lắng nghe... (Nói bất kỳ lúc nào!)" << std::endl;
    std::cout << "Nhấn Ctrl+C để thoát.\n" << std::endl;

    // Bắt đầu capture audio
    SDL_PauseAudioDevice(audio_dev, 0);

    // Vòng lặp liên tục để lắng nghe và phát hiện giọng nói
    while (g_listening) {
        // Kiểm tra xem có giọng nói không
        bool has_voice = has_voice_activity(g_audio_buffer);

        // Bắt đầu ghi âm khi phát hiện giọng nói
        if (has_voice && !collecting_speech) {
            collecting_speech = true;
            speech_duration_ms = 0;
            silence_timeout_ms = 0;
            speech_buffer.clear();
            speech_buffer = g_audio_buffer;
            std::cout << "🔴 Đang ghi âm..." << std::endl;
        }

        // Cập nhật trạng thái ghi âm
        if (collecting_speech) {
            speech_buffer = g_audio_buffer;
            speech_duration_ms += 50;

            if (has_voice) {
                silence_timeout_ms = 0;
            } else {
                silence_timeout_ms += 50;
            }

            // Kiểm tra xem đã kết thúc giọng nói không
            if (speech_duration_ms >= MIN_SPEECH_DURATION && silence_timeout_ms >= SILENCE_DURATION) {
                collecting_speech = false;
                std::cout << "⏹ Ghi âm hoàn thành!" << std::endl;

                const std::string transcript = transcribe_speech(ctx, speech_buffer);
                if (!transcript.empty()) {
                    std::future<Action> fut = commands.process_async(transcript);
                    print_action(fut.get());
                }

                // Xóa global buffer để bắt đầu lại
                g_audio_buffer.clear();
                std::cout << "🎤 Lắng nghe tiếp...\n" << std::endl;
            }
        }

        // Giới hạn kích thước buffer
        limit_buffer_size(g_audio_buffer);

        // Chờ một chút trước khi check lại
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    SDL_PauseAudioDevice(audio_dev, 1);
}

// Cleanup và giải phóng resources
void cleanup(struct whisper_context* ctx, SDL_AudioDeviceID audio_dev) {
    SDL_CloseAudioDevice(audio_dev);
    whisper_free(ctx);
    SDL_Quit();
    std::cout << "\n✅ Đã thoát." << std::endl;
}

int main() {
    // Register signal handler for Ctrl+C
    std::signal(SIGINT, signal_handler);

    // Initialize Whisper model
    struct whisper_context* ctx = init_whisper();
    if (!ctx) return 1;

    // Initialize SDL Audio
    SDL_AudioDeviceID audio_dev;
    if (!init_sdl_audio(audio_dev)) {
        whisper_free(ctx);
        return 1;
    }

    CommandProcessor commands;
    listening_loop(ctx, audio_dev, commands);

    // Cleanup
    cleanup(ctx, audio_dev);
    return 0;
}