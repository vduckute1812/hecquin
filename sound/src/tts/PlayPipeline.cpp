#include "tts/PlayPipeline.hpp"

#include "tts/PiperSpeech.hpp"
#include "tts/backend/PiperFallbackBackend.hpp"
#include "tts/backend/PiperSpawn.hpp"
#include "tts/playback/StreamingSdlPlayer.hpp"

#include <sys/wait.h>

#include <cstdint>
#include <iostream>
#include <vector>

namespace hecquin::tts {

namespace {

constexpr int kPiperSampleRate = 22050;

} // namespace

bool speak_and_play(const std::string& text, const std::string& model_path) {
    std::cout << "🗣️  Piper will say: " << text << std::endl;
    std::vector<std::int16_t> samples;
    int sr = 0;
    if (!piper_synthesize_to_buffer(text, model_path, samples, sr)) {
        return false;
    }
    std::cout << "📊 Loaded " << samples.size() << " samples ("
              << static_cast<float>(samples.size()) / kPiperSampleRate
              << " s)" << std::endl;
    return sdl_play_s16_mono_22k(samples);
}

bool speak_and_play_streaming(const std::string& text,
                              const std::string& model_path) {
    std::cout << "🗣️  Piper will say (streaming): " << text << std::endl;

    hecquin::tts::playback::StreamingSdlPlayer player;
    if (!player.start(kPiperSampleRate)) {
        // SDL init / device open failed — fall back to the buffered
        // path so the user still hears the reply.
        return speak_and_play(text, model_path);
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
        return speak_and_play(text, model_path);
    }

    player.finish();
    player.wait_until_drained();
    player.stop();

    if (WIFEXITED(result.exit_status) && WEXITSTATUS(result.exit_status) != 0) {
        std::cerr << "[piper] exited with " << WEXITSTATUS(result.exit_status)
                  << std::endl;
        return false;
    }
    if (WIFSIGNALED(result.exit_status)) {
        std::cerr << "[piper] killed by signal " << WTERMSIG(result.exit_status)
                  << std::endl;
        return false;
    }
    return true;
}

} // namespace hecquin::tts
