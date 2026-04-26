#pragma once

#include "ai/LocalIntentMatcher.hpp"
#include "config/AppConfig.hpp"
#include "voice/VoiceApp.hpp"
#include "voice/VoiceListener.hpp"

#include <memory>
#include <optional>
#include <string>

namespace hecquin::learning {

class LearningStore;
class ProgressTracker;
class PronunciationDrillProcessor;
struct PronunciationDrillConfig;

namespace cli {

/**
 * Shared bootstrap for the `english_tutor` and `pronunciation_drill`
 * binaries.  Wraps the identical scaffolding both mains used to carry:
 *
 *   1. `VoiceApp` init (SIGINT, whisper, config, mic).
 *   2. Bake the absolute pronunciation model / vocab paths when the .env
 *      values are relative (keeps the binary runnable regardless of cwd).
 *   3. Open the `LearningStore` with a readable warning on failure.
 *   4. Construct + load the `PronunciationDrillProcessor`.
 *   5. Build the `LocalIntentMatcher` config from learning phrase lists.
 *   6. Wire the `pipeline_events` telemetry sink into `VoiceListener`.
 *
 * Callers own the `VoiceListener` and wire whichever processor callbacks
 * they want.  Destruction closes everything in LIFO order.
 */
class LearningApp {
public:
    struct Options {
        std::string whisper_model_path;
        std::string piper_model_path;
        std::string config_path;
        std::string prompts_dir;
        std::string baked_pronunciation_model_path;  ///< empty → leave config alone
        std::string baked_pronunciation_vocab_path;
        std::string progress_mode = "lesson";         ///< ProgressTracker kind
        /**
         * True for english_tutor (keeps a *second* ProgressTracker("drill")
         * so lesson and drill attempts log under distinct sessions).
         */
        bool open_secondary_drill_progress = false;
    };

    explicit LearningApp(Options opts);
    ~LearningApp();

    LearningApp(const LearningApp&) = delete;
    LearningApp& operator=(const LearningApp&) = delete;

    /**
     * Runs steps 1–5.  Returns false on whisper / mic failure; drill model
     * absence is warned about but does not fail the init (the binary still
     * runs, with degraded scoring).
     */
    bool init();

    /** Attach the pipeline-event sink to the listener — no-op if DB is closed. */
    void wire_pipeline_sink(VoiceListener& listener) const;

    /**
     * Install the standard drill-mode callbacks on `listener`:
     *   - `setDrillCallback` → `drill().score(pcm, transcript)`
     *   - `setDrillAnnounceCallback` → `drill().pick_and_announce()`
     *
     * Both CLIs wire these identically; keeping them here removes the
     * last piece of parallel wiring in the mains.
     */
    void wire_drill_callbacks(VoiceListener& listener);

    /** Matcher config built from the learning-phrase lists in AppConfig. */
    hecquin::ai::LocalIntentMatcherConfig matcher_config() const;

    /**
     * Tier-4 #16: install the IdentifyUser → LearningStore::upsert_user
     * bridge so a "this is Mia" / "I'm Liam" intent updates the in-app
     * `current_user_id_`.  Subsequent progress writes can be namespaced
     * per user once the lower-level writers grow a user_id parameter.
     */
    void wire_user_identification(VoiceListener& listener);

    /**
     * Tier-4 #17: speak a short welcome-back line summarising the
     * current user's last session score and weakest phoneme.  No-op
     * when the store is closed or there is no usable history.  Safe to
     * call before `listener.run()` — uses the same Piper backend as
     * `VoiceApp::speak_capability_summary`.
     */
    void speak_welcome_back();

    voice::VoiceApp&             voice()          { return voice_app_; }
    LearningStore&               store()          { return *store_; }
    bool                         store_open()     const;
    PronunciationDrillProcessor& drill()          { return *drill_; }
    ProgressTracker&             progress()       { return *progress_; }
    ProgressTracker*             drill_progress() { return drill_progress_.get(); }
    bool                         drill_ready()    const { return drill_ok_; }
    AppConfig&                   config()         { return voice_app_.config(); }
    const std::string&           piper_model_path() const {
        return voice_app_.piper_model_path();
    }

    /** Close progress + voice in LIFO order. Idempotent. */
    void shutdown();

private:
    void bake_pronunciation_paths_();

    Options                                        opts_;
    voice::VoiceApp                                voice_app_;
    std::unique_ptr<LearningStore>                 store_;
    std::unique_ptr<ProgressTracker>               progress_;
    std::unique_ptr<ProgressTracker>               drill_progress_;
    std::unique_ptr<PronunciationDrillProcessor>   drill_;
    bool                                           drill_ok_ = false;
    bool                                           shut_ = false;
    /**
     * Tier-4 #16: row id of the most recently identified user.  Stays
     * `nullopt` until a `IdentifyUser` intent fires (or future builds
     * load a persisted "last user" preference at boot).  Reads default
     * to "all users" when absent so single-user rigs don't regress.
     */
    std::optional<int64_t>                         current_user_id_;
};

} // namespace cli
} // namespace hecquin::learning
