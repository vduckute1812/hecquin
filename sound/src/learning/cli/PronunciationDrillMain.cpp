#include "ai/CommandProcessor.hpp"
#include "cli/DefaultPaths.hpp"
#include "learning/PronunciationDrillProcessor.hpp"
#include "learning/cli/LearningApp.hpp"
#include "learning/store/LearningStore.hpp"
#include "voice/MusicWiring.hpp"
#include "voice/VoiceListener.hpp"

#include <iostream>

int main() {
    hecquin::learning::cli::LearningApp app({
        /*whisper_model_path=*/DEFAULT_MODEL_PATH,
        /*piper_model_path=*/DEFAULT_PIPER_MODEL_PATH,
        /*config_path=*/DEFAULT_CONFIG_PATH,
        /*prompts_dir=*/DEFAULT_PROMPTS_DIR,
        /*baked_pron_model=*/DEFAULT_PRONUNCIATION_MODEL_PATH,
        /*baked_pron_vocab=*/DEFAULT_PRONUNCIATION_VOCAB_PATH,
        /*progress_mode=*/"drill",
        /*open_secondary_drill_progress=*/false,
    });
    if (!app.init()) return 1;

    AppConfig& cfg = app.config();

    auto matcher_cfg = app.matcher_config();
    CommandProcessor commands(cfg.ai, std::move(matcher_cfg));
    VoiceListenerConfig vcfg;
    vcfg.apply_env_overrides();
    VoiceListener listener(app.voice().whisper(), app.voice().capture(), commands,
                           app.voice().running(), app.piper_model_path(), vcfg);
    app.wire_pipeline_sink(listener);
    app.wire_drill_callbacks(listener);

    auto music = hecquin::voice::install_music_wiring(listener, cfg.music);

    listener.setInitialMode(ListenerMode::Drill);

    // Speak the first sentence immediately so the user is not greeted by silence.
    app.drill().pick_and_announce();

    std::cout << "[pronunciation_drill] Drill mode — say the sentence I just read.\n"
              << "  Say \"exit drill\" to quit."
              << std::endl;
    listener.run();

    app.shutdown();
    std::cout << "\n[pronunciation_drill] exited cleanly." << std::endl;
    return 0;
}
