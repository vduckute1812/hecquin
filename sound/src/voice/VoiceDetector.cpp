#include "ai/CommandProcessor.hpp"
#include "ai/LocalIntentMatcher.hpp"
#include "cli/DefaultPaths.hpp"
#include "voice/MusicWiring.hpp"
#include "voice/VoiceApp.hpp"
#include "voice/VoiceListener.hpp"

#include <iostream>

int main() {
    hecquin::voice::VoiceApp app({
        DEFAULT_MODEL_PATH,
        DEFAULT_PIPER_MODEL_PATH,
        DEFAULT_CONFIG_PATH,
        DEFAULT_PROMPTS_DIR,
    });
    if (!app.init()) return 1;

    auto matcher_cfg = hecquin::ai::LocalIntentMatcherConfig::make_from_learning(
        app.config().learning);
    CommandProcessor commands(std::move(app.config().ai), std::move(matcher_cfg));

    VoiceListenerConfig vcfg;
    vcfg.apply_env_overrides();
    VoiceListener listener(app.whisper(), app.capture(), commands,
                           app.running(), app.piper_model_path(), vcfg);

    auto music = hecquin::voice::install_music_wiring(listener, app.config().music);

    listener.run();

    app.shutdown();
    std::cout << "\n[voice_detector] exited cleanly." << std::endl;
    return 0;
}
