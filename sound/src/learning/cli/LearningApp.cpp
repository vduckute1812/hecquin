#include "learning/cli/LearningApp.hpp"

#include "learning/ProgressTracker.hpp"
#include "learning/PronunciationDrillProcessor.hpp"
#include "learning/store/LearningStore.hpp"
#include "voice/VoiceListener.hpp"

#include <iostream>

namespace hecquin::learning::cli {

namespace {

voice::VoiceApp::Options to_voice_options(const LearningApp::Options& opts) {
    return voice::VoiceApp::Options{
        opts.whisper_model_path,
        opts.piper_model_path,
        opts.config_path,
        opts.prompts_dir,
    };
}

} // namespace

LearningApp::LearningApp(Options opts)
    : opts_(std::move(opts)), voice_app_(to_voice_options(opts_)) {}

LearningApp::~LearningApp() { shutdown(); }

bool LearningApp::init() {
    if (!voice_app_.init()) return false;

    bake_pronunciation_paths_();

    AppConfig& cfg = voice_app_.config();
    store_ = std::make_unique<LearningStore>(cfg.learning.db_path, cfg.ai.embedding_dim);
    if (!store_->open()) {
        std::cerr << "[learning_app] Learning DB unavailable; continuing without RAG / progress."
                  << std::endl;
    }

    progress_ = std::make_unique<ProgressTracker>(*store_, opts_.progress_mode);
    if (opts_.open_secondary_drill_progress) {
        drill_progress_ = std::make_unique<ProgressTracker>(*store_, "drill");
    }

    // Drill processor.  The english_tutor binary treats drill as a sub-mode,
    // so it passes a distinct `drill_progress_`; pronunciation_drill uses
    // `progress_` directly (its home mode is drill).
    ProgressTracker* drill_tracker =
        drill_progress_ ? drill_progress_.get() : progress_.get();
    PronunciationDrillConfig dcfg;
    dcfg.pass_threshold_0_100 = cfg.learning.drill_pass_threshold;
    drill_ = std::make_unique<PronunciationDrillProcessor>(
        cfg, store_.get(), drill_tracker,
        voice_app_.piper_model_path(), dcfg);
    drill_ok_ = drill_->load();
    if (!drill_ok_) {
        std::cerr << "[learning_app] Pronunciation model unavailable — "
                  << "scoring will be degraded.\n"
                  << "  Run: ./dev.sh pronunciation:install" << std::endl;
    }

    return true;
}

void LearningApp::bake_pronunciation_paths_() {
    AppConfig& cfg = voice_app_.config();
    if (cfg.pronunciation.model_path.empty() ||
        cfg.pronunciation.model_path.front() != '/') {
        if (!opts_.baked_pronunciation_model_path.empty()) {
            cfg.pronunciation.model_path = opts_.baked_pronunciation_model_path;
        }
    }
    if (cfg.pronunciation.vocab_path.empty() ||
        cfg.pronunciation.vocab_path.front() != '/') {
        if (!opts_.baked_pronunciation_vocab_path.empty()) {
            cfg.pronunciation.vocab_path = opts_.baked_pronunciation_vocab_path;
        }
    }
}

bool LearningApp::store_open() const {
    return store_ && store_->is_open();
}

void LearningApp::wire_pipeline_sink(VoiceListener& listener) const {
    if (!store_open()) return;
    LearningStore* s = store_.get();
    listener.setPipelineEventSink([s](const PipelineEvent& e) {
        s->record_pipeline_event(e.event, e.outcome, e.duration_ms, e.attrs_json);
    });
}

void LearningApp::wire_drill_callbacks(VoiceListener& listener) {
    PronunciationDrillProcessor* drill = drill_.get();
    if (!drill) return;
    listener.setDrillCallback([drill](const Utterance& u) {
        return drill->score(u.pcm_16k, u.transcript);
    });
    listener.setDrillAnnounceCallback([drill]() {
        drill->pick_and_announce();
    });
}

hecquin::ai::LocalIntentMatcherConfig LearningApp::matcher_config() const {
    // VoiceApp::config() is non-const; this helper doesn't mutate config_ — a
    // const_cast keeps the public `LearningApp::matcher_config()` const without
    // forcing a VoiceApp change for one call site.
    const AppConfig& cfg = const_cast<LearningApp*>(this)->voice_app_.config();
    return hecquin::ai::LocalIntentMatcherConfig::make_from_learning(cfg.learning);
}

void LearningApp::shutdown() {
    if (shut_) return;
    shut_ = true;
    if (drill_progress_) drill_progress_->close();
    if (progress_) progress_->close();
    voice_app_.shutdown();
}

} // namespace hecquin::learning::cli
