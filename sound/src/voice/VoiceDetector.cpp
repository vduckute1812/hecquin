#include "ai/CommandProcessor.hpp"
#include "voice/VoiceApp.hpp"
#include "voice/VoiceListener.hpp"

#include <iostream>

// Compile-time defaults injected by CMake (see hecquin_set_runtime_defaults);
// the fallbacks below keep the file compilable outside the project build.
#ifndef DEFAULT_MODEL_PATH
#define DEFAULT_MODEL_PATH ".env/models/ggml-base.bin"
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

int main() {
    hecquin::voice::VoiceApp app({
        DEFAULT_MODEL_PATH,
        DEFAULT_PIPER_MODEL_PATH,
        DEFAULT_CONFIG_PATH,
        DEFAULT_PROMPTS_DIR,
    });
    if (!app.init()) return 1;

    CommandProcessor commands(std::move(app.config().ai));
    VoiceListener listener(app.whisper(), app.capture(), commands,
                           app.running(), app.piper_model_path());
    listener.run();

    app.shutdown();
    std::cout << "\n[voice_detector] exited cleanly." << std::endl;
    return 0;
}
