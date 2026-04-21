#pragma once

#include "actions/Action.hpp"
#include "config/AppConfig.hpp"
#include "learning/pronunciation/PhonemeModel.hpp"
#include "learning/pronunciation/G2P.hpp"
#include "learning/pronunciation/CtcAligner.hpp"
#include "learning/pronunciation/PronunciationScorer.hpp"
#include "learning/prosody/PitchTracker.hpp"
#include "learning/prosody/IntonationScorer.hpp"

#include <memory>
#include <string>
#include <vector>

namespace hecquin::learning {

class LearningStore;
class ProgressTracker;

struct PronunciationDrillConfig {
    int pass_threshold_0_100 = 75;
    /** Fall-back sentences when neither a sentences file nor DB content is available. */
    std::vector<std::string> fallback_sentences{
        "Good morning, how are you today?",
        "Through the rain I saw the rainbow.",
        "Would you like a cup of tea?",
        "She sells seashells by the seashore.",
        "I think it is going to rain tomorrow.",
    };
};

/**
 * End-to-end orchestrator for the prompted pronunciation / intonation drill.
 *
 *   1. `pick_and_announce()` chooses a target sentence, runs Piper to speak
 *      it, and stashes the reference pitch contour for scoring.  It is called
 *      by the listener when entering drill mode and after each scored attempt.
 *   2. `score(Utterance)` analyses the learner's PCM against the last
 *      announced reference — runs the phoneme model, forced alignment,
 *      per-phoneme GOP, and pitch/intonation scoring.  Logs to the progress
 *      tracker and returns a `PronunciationFeedbackAction` wrapped in a
 *      routing-compatible `Action`.
 *
 * When the onnxruntime model / espeak-ng / curriculum resources are missing,
 * the processor gracefully returns an action with `reply` explaining the
 * situation instead of crashing, mirroring how `EnglishTutorProcessor`
 * handles the "cloud API not configured" path.
 */
class PronunciationDrillProcessor {
public:
    PronunciationDrillProcessor(const AppConfig& app_cfg,
                                LearningStore* store,
                                ProgressTracker* progress,
                                const std::string& piper_model_path,
                                PronunciationDrillConfig cfg = {});
    ~PronunciationDrillProcessor();

    PronunciationDrillProcessor(const PronunciationDrillProcessor&) = delete;
    PronunciationDrillProcessor& operator=(const PronunciationDrillProcessor&) = delete;

    /** Load models + sentence pool.  Returns true when scoring will be available. */
    bool load();

    /** Whether scoring will succeed (model + espeak + pitch tracker all ok). */
    [[nodiscard]] bool available() const;

    /** Currently announced reference (empty until `pick_and_announce` has run). */
    [[nodiscard]] const std::string& current_reference() const { return current_reference_; }

    /**
     * Pick the next drill sentence, synthesise it via Piper, speak it, and
     * cache its pitch contour for scoring.  Returns the reference text.
     */
    std::string pick_and_announce();

    /**
     * Score one learner attempt against the last announced reference.
     * Emits a `PronunciationFeedbackAction`.  Tests should inject a fake
     * phoneme model via `set_phoneme_model_for_test()` so `infer()` returns
     * a pre-baked emissions matrix — that keeps this API single-purposed.
     */
    Action score(const std::vector<float>& user_pcm_16k, const std::string& transcript);

    /** Inject a pre-built phoneme model (tests use fake emissions). */
    void set_phoneme_model_for_test(std::unique_ptr<pronunciation::PhonemeModel> m);

    /** Inject a sentence list directly (tests skip file / DB loading). */
    void set_sentences_for_test(std::vector<std::string> s);

    /** Expose the model vocab for tests / external inspection. */
    const pronunciation::PhonemeVocab& vocab() const;

    /** Expose the pitch tracker so callers can reuse its config. */
    const prosody::PitchTracker& pitch_tracker() const { return pitch_tracker_user_; }

private:
    std::string next_sentence_();
    void compute_reference_pitch_(const std::string& text);
    static std::vector<float> int16_to_float_(const std::vector<int16_t>& in);

    const AppConfig& app_cfg_;
    LearningStore* store_;
    ProgressTracker* progress_;
    std::string piper_model_path_;
    PronunciationDrillConfig cfg_;

    std::unique_ptr<pronunciation::PhonemeModel> phoneme_model_;
    std::unique_ptr<pronunciation::G2P> g2p_;
    pronunciation::CtcAligner aligner_;
    pronunciation::PronunciationScorer pron_scorer_;
    prosody::PitchTracker pitch_tracker_user_;    // 16 kHz (capture rate)
    prosody::PitchTracker pitch_tracker_piper_;   // Piper's native rate
    prosody::IntonationScorer intonation_scorer_;

    std::vector<std::string> sentence_pool_;
    std::size_t next_sentence_idx_ = 0;
    std::string current_reference_;
    prosody::PitchContour current_reference_contour_;

    bool loaded_ = false;
};

} // namespace hecquin::learning
