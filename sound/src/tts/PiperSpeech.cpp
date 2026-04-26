#include "tts/PiperSpeech.hpp"

#include "tts/PlayPipeline.hpp"
#include "tts/backend/PiperFallbackBackend.hpp"
#include "tts/backend/PiperShellBackend.hpp"
#include "tts/playback/BufferedSdlPlayer.hpp"

#include <memory>

namespace {

hecquin::tts::backend::IPiperBackend& default_backend() {
    static auto backend = hecquin::tts::backend::make_default_backend();
    return *backend;
}

} // namespace

bool piper_synthesize_wav(const std::string& text,
                          const std::string& model_path,
                          const std::string& output_wav_path) {
    return hecquin::tts::backend::PiperShellBackend::synthesize_to_wav(
        text, model_path, output_wav_path);
}

bool piper_synthesize_to_buffer(const std::string& text,
                                const std::string& model_path,
                                std::vector<int16_t>& samples_out,
                                int& sample_rate_out) {
    return default_backend().synthesize(text, model_path, samples_out,
                                        sample_rate_out);
}

bool sdl_play_s16_mono_22k(const std::vector<int16_t>& samples) {
    return hecquin::tts::playback::play_mono_22k(samples);
}

bool piper_speak_and_play(const std::string& text,
                          const std::string& model_path) {
    return hecquin::tts::speak_and_play(text, model_path);
}

bool piper_speak_and_play_streaming(const std::string& text,
                                    const std::string& model_path) {
    return hecquin::tts::speak_and_play_streaming(text, model_path, nullptr);
}

bool piper_speak_and_play_streaming(const std::string& text,
                                    const std::string& model_path,
                                    const std::atomic<bool>* abort_flag) {
    return hecquin::tts::speak_and_play_streaming(text, model_path, abort_flag);
}
