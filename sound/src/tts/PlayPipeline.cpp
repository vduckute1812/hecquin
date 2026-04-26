#include "tts/PlayPipeline.hpp"

#include "tts/PiperSampleRate.hpp"
#include "tts/PiperSpeech.hpp"
#include "tts/backend/PiperFallbackBackend.hpp"
#include "tts/backend/PiperSpawn.hpp"
#include "tts/backend/PiperWaitStatus.hpp"
#include "tts/playback/StreamingSdlPlayer.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

namespace hecquin::tts {

namespace {

/**
 * Tear down a streaming player after a synth completes.  When the
 * player was aborted mid-stream we drop the queued samples
 * immediately; otherwise we wait until the DAC has drained the
 * remaining samples so the tail isn't clipped.
 */
void finalize_streaming_player(hecquin::tts::playback::StreamingSdlPlayer& player,
                               bool aborted) {
    if (aborted) {
        player.stop();
        return;
    }
    player.finish();
    player.wait_until_drained();
    player.stop();
}

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
                              const std::string& model_path,
                              const std::atomic<bool>* abort_flag) {
    std::cout << "🗣️  Piper will say (streaming): " << text << std::endl;

    hecquin::tts::playback::StreamingSdlPlayer player;
    if (!player.start(kPiperSampleRate)) {
        // SDL init / device open failed — fall back to the buffered
        // path so the user still hears the reply.
        return speak_and_play(text, model_path);
    }

    bool aborted = false;
    const hecquin::tts::backend::PiperSpawnResult result =
        hecquin::tts::backend::run_pipe_synth(
            text, model_path,
            [&](const std::int16_t* p, std::size_t n) {
                if (abort_flag && abort_flag->load(std::memory_order_acquire)) {
                    // First abort wins; subsequent calls should not
                    // try to push more samples even if the spawn keeps
                    // emitting them.
                    aborted = true;
                    return false;
                }
                player.push(p, n);
                return true;
            });

    if (!result.spawned) {
        // Spawn or pipe setup failed — fall back to buffered path.
        player.stop();
        return speak_and_play(text, model_path);
    }

    finalize_streaming_player(player, aborted);
    if (aborted) {
        // Caller asked for barge-in; report not-OK without logging the
        // wait status (the abort is the expected outcome here).
        return false;
    }

    return hecquin::tts::backend::log_piper_wait_status(result.exit_status);
}

} // namespace hecquin::tts
