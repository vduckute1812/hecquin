#include "voice/PrewarmService.hpp"

#include "common/EnvParse.hpp"
#include "tts/PiperSpeech.hpp"
#include "voice/WhisperEngine.hpp"

#include <iostream>
#include <thread>
#include <utility>
#include <vector>

namespace hecquin::voice {

void PrewarmService::apply_env_overrides() {
    namespace env = hecquin::common::env;
    bool flag = false;
    if (env::parse_bool("HECQUIN_PREWARM", flag)) {
        set_enabled(flag);
    }
}

void PrewarmService::warm_piper(std::string model_path) {
    if (!enabled_.load(std::memory_order_acquire)) return;
    if (model_path.empty()) return;
    std::thread([model = std::move(model_path)] {
        std::vector<std::int16_t> samples;
        int sr = 0;
        // " " (single space) is the cheapest non-empty input that still
        // exercises the spawn + model-load path; the resulting PCM is
        // discarded so the user never hears it.
        const bool ok = piper_synthesize_to_buffer(" ", model, samples, sr);
        if (!ok) {
            std::cerr << "[prewarm] piper warm-up skipped (model=" << model << ")"
                      << std::endl;
        }
    }).detach();
}

void PrewarmService::warm_whisper(WhisperEngine& engine) {
    if (!enabled_.load(std::memory_order_acquire)) return;
    std::thread([&engine] {
        // 250 ms of silence is enough for whisper to allocate buffers
        // and JIT-warm its inner loops without dispatching a real
        // transcription run on real audio.
        std::vector<float> silence(4000, 0.0f);
        (void)engine.transcribe(silence);
    }).detach();
}

} // namespace hecquin::voice
