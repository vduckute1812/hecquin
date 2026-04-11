#include "tts/PiperSpeech.hpp"

#include <SDL.h>

#include <iostream>
#include <string>

#ifndef DEFAULT_PIPER_MODEL_PATH
#define DEFAULT_PIPER_MODEL_PATH ".env/shared/models/piper/en_US-lessac-medium.onnx"
#endif

constexpr const char* FALLBACK_TEXT = "Hello! I am your robot tutor. How can I help you today?";

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

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if ((arg == "-m" || arg == "--model") && i + 1 < argc) {
            model_path = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_file = argv[++i];
            save_to_file = true;
        } else if (arg[0] != '-') {
            text = arg;
        }
    }

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

    if (save_to_file) {
        if (!piper_synthesize_wav(text, model_path, output_file)) {
            return 1;
        }
        std::cout << "✅ Tổng hợp thành công!" << std::endl;
        std::cout << "💾 Đã lưu audio vào: " << output_file << std::endl;
        return 0;
    }

    if (!piper_speak_and_play(text, model_path)) {
        if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0) {
            SDL_Quit();
        }
        return 1;
    }

    std::cout << "✅ Hoàn tất!" << std::endl;

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0) {
        SDL_Quit();
    }
    return 0;
}
