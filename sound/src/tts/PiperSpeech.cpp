#include "tts/PiperSpeech.hpp"

#include "tts/backend/PiperFallbackBackend.hpp"
#include "tts/backend/PiperShellBackend.hpp"
#include "tts/backend/PiperSpawn.hpp"
#include "tts/playback/BufferedSdlPlayer.hpp"
#include "tts/playback/StreamingSdlPlayer.hpp"

#include <iostream>
#include <memory>
#include <sys/wait.h>

namespace {

constexpr int kPiperSampleRate = 22050;

hecquin::tts::backend::IPiperBackend& default_backend() {
    static auto backend = hecquin::tts::backend::make_default_backend();
    return *backend;
}

} // namespace

bool piper_synthesize_wav(const std::string& text,
                          const std::string& model_path,
                          const std::string& output_wav_path) {
    return hecquin::tts::backend::PiperShellBackend::synthesize_to_wav(text, model_path, output_wav_path);
}

bool piper_synthesize_to_buffer(const std::string& text,
                                const std::string& model_path,
                                std::vector<int16_t>& samples_out,
                                int& sample_rate_out) {
    return default_backend().synthesize(text, model_path, samples_out, sample_rate_out);
}

bool sdl_play_s16_mono_22k(const std::vector<int16_t>& samples) {
    return hecquin::tts::playback::play_mono_22k(samples);
}

bool piper_speak_and_play(const std::string& text, const std::string& model_path) {
    std::cout << "🗣️  Piper will say: " << text << std::endl;
    std::vector<int16_t> samples;
    int sr = 0;
    if (!piper_synthesize_to_buffer(text, model_path, samples, sr)) {
        return false;
    }
    std::cout << "📊 Loaded " << samples.size() << " samples ("
              << static_cast<float>(samples.size()) / kPiperSampleRate
              << " s)" << std::endl;
    return sdl_play_s16_mono_22k(samples);
}

bool piper_speak_and_play_streaming(const std::string& text,
                                    const std::string& model_path) {
    std::cout << "🗣️  Piper will say (streaming): " << text << std::endl;

    hecquin::tts::playback::StreamingSdlPlayer player;
    if (!player.start(kPiperSampleRate)) {
        // SDL init/open failed — best-effort fallback to the buffered path.
        return piper_speak_and_play(text, model_path);
    }

    const hecquin::tts::backend::PiperSpawnResult result =
        hecquin::tts::backend::run_pipe_synth(
            text, model_path,
            [&](const std::int16_t* p, std::size_t n) {
                player.push(p, n);
                return true;
            });

    if (!result.spawned) {
        // Spawn or pipe setup failed — fall back to buffered path.
        player.stop();
        return piper_speak_and_play(text, model_path);
    }

    player.finish();
    player.wait_until_drained();
    player.stop();

    if (WIFEXITED(result.exit_status) && WEXITSTATUS(result.exit_status) != 0) {
        std::cerr << "[piper] exited with " << WEXITSTATUS(result.exit_status) << std::endl;
        return false;
    }
    if (WIFSIGNALED(result.exit_status)) {
        std::cerr << "[piper] killed by signal " << WTERMSIG(result.exit_status) << std::endl;
        return false;
    }
    return true;
}
