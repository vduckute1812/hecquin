#include "cli/DefaultPaths.hpp"
#include "tts/PiperSpeech.hpp"

#include <SDL.h>

#include <iostream>
#include <string>

constexpr const char* FALLBACK_TEXT = "Hello! I am your robot tutor. How can I help you today?";

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options] \"text to speak\"" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -m, --model <path>   Path to the Piper model (.onnx)" << std::endl;
    std::cout << "  -o, --output <path>  Save audio to a WAV file (do not play)" << std::endl;
    std::cout << "  -h, --help           Show help" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
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
        std::cout << "Enter the text to speak (press Enter to confirm):" << std::endl;
        std::cout << "> ";
        std::getline(std::cin, text);

        if (text.empty()) {
            text = FALLBACK_TEXT;
            std::cout << "Using sample text: \"" << text << "\"" << std::endl;
        }
    }

    std::cout << "📝 Text: \"" << text << "\"" << std::endl;
    std::cout << "🎯 Model: " << model_path << std::endl;

    if (save_to_file) {
        if (!piper_synthesize_wav(text, model_path, output_file)) {
            return 1;
        }
        std::cout << "✅ Synthesis successful!" << std::endl;
        std::cout << "💾 Audio saved to: " << output_file << std::endl;
        return 0;
    }

    if (!piper_speak_and_play(text, model_path)) {
        if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0) {
            SDL_Quit();
        }
        return 1;
    }

    std::cout << "✅ Done!" << std::endl;

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0) {
        SDL_Quit();
    }
    return 0;
}
