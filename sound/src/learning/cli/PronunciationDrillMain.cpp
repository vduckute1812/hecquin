#include "ai/CommandProcessor.hpp"
#include "learning/store/LearningStore.hpp"
#include "learning/ProgressTracker.hpp"
#include "learning/PronunciationDrillProcessor.hpp"
#include "voice/VoiceApp.hpp"
#include "voice/VoiceListener.hpp"

#include <iostream>

#ifndef DEFAULT_MODEL_PATH
#define DEFAULT_MODEL_PATH ".env/shared/models/ggml-base.bin"
#endif
#ifndef DEFAULT_PIPER_MODEL_PATH
#define DEFAULT_PIPER_MODEL_PATH ".env/shared/models/piper/en_US-lessac-medium.onnx"
#endif
#ifndef DEFAULT_CONFIG_PATH
#define DEFAULT_CONFIG_PATH ConfigStore::kDefaultPath
#endif
#ifndef DEFAULT_PROMPTS_DIR
#define DEFAULT_PROMPTS_DIR ""
#endif
#ifndef DEFAULT_PRONUNCIATION_MODEL_PATH
#define DEFAULT_PRONUNCIATION_MODEL_PATH ""
#endif
#ifndef DEFAULT_PRONUNCIATION_VOCAB_PATH
#define DEFAULT_PRONUNCIATION_VOCAB_PATH ""
#endif

int main() {
    hecquin::voice::VoiceApp app({
        DEFAULT_MODEL_PATH,
        DEFAULT_PIPER_MODEL_PATH,
        DEFAULT_CONFIG_PATH,
        DEFAULT_PROMPTS_DIR,
    });
    if (!app.init()) return 1;

    AppConfig& cfg = app.config();

    // Prefer the absolute path baked by CMake when the env file did not point
    // at a model — this keeps the binary runnable regardless of cwd.
    if (cfg.pronunciation.model_path.empty() ||
        cfg.pronunciation.model_path.front() != '/') {
        const std::string baked = DEFAULT_PRONUNCIATION_MODEL_PATH;
        if (!baked.empty()) cfg.pronunciation.model_path = baked;
    }
    if (cfg.pronunciation.vocab_path.empty() ||
        cfg.pronunciation.vocab_path.front() != '/') {
        const std::string baked = DEFAULT_PRONUNCIATION_VOCAB_PATH;
        if (!baked.empty()) cfg.pronunciation.vocab_path = baked;
    }

    hecquin::learning::LearningStore store(cfg.learning.db_path, cfg.ai.embedding_dim);
    if (!store.open()) {
        std::cerr << "[pronunciation_drill] Learning DB unavailable; progress will not be recorded."
                  << std::endl;
    }

    hecquin::learning::ProgressTracker progress(store, "drill");

    hecquin::learning::PronunciationDrillConfig dcfg;
    dcfg.pass_threshold_0_100 = cfg.learning.drill_pass_threshold;
    hecquin::learning::PronunciationDrillProcessor drill(cfg, &store, &progress,
                                                         app.piper_model_path(), dcfg);

    if (!drill.load()) {
        std::cerr << "[pronunciation_drill] Pronunciation model not available.\n"
                  << "  Run: ./dev.sh pronunciation:install\n"
                  << "  (Falling back to intonation-only feedback.)"
                  << std::endl;
    }

    CommandProcessor commands(cfg.ai);
    VoiceListener listener(app.whisper(), app.capture(), commands,
                           app.running(), app.piper_model_path());

    listener.setDrillCallback([&drill](const Utterance& u) {
        return drill.score(u.pcm_16k, u.transcript);
    });
    listener.setDrillAnnounceCallback([&drill]() {
        drill.pick_and_announce();
    });
    listener.setInitialMode(ListenerMode::Drill);

    // Speak the first sentence immediately so the user is not greeted by silence.
    drill.pick_and_announce();

    std::cout << "[pronunciation_drill] Drill mode — say the sentence I just read.\n"
              << "  Say \"exit drill\" to quit."
              << std::endl;
    listener.run();

    progress.close();
    app.shutdown();
    std::cout << "\n[pronunciation_drill] exited cleanly." << std::endl;
    return 0;
}
