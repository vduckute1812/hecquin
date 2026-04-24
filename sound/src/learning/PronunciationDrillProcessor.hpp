#pragma once

#include "actions/Action.hpp"
#include "config/AppConfig.hpp"
#include "learning/pronunciation/G2P.hpp"
#include "learning/pronunciation/PhonemeModel.hpp"
#include "learning/pronunciation/drill/DrillProgressLogger.hpp"
#include "learning/pronunciation/drill/DrillReferenceAudio.hpp"
#include "learning/pronunciation/drill/DrillScoringPipeline.hpp"
#include "learning/pronunciation/drill/DrillSentencePicker.hpp"
#include "learning/prosody/PitchTracker.hpp"

#include <memory>
#include <string>
#include <vector>

namespace hecquin::learning {

class LearningStore;
class ProgressTracker;

struct PronunciationDrillConfig {
    int pass_threshold_0_100 = 75;

    /**
     * Maximum number of weakest-word hints to surface in the spoken reply.
     * `PronunciationFeedbackAction::to_reply()` only mentions `lowest_words[0]`
     * today, but progress logging + any future UI consumes the full list.
     * Keeping this small keeps replies conversational.
     */
    int max_feedback_words = 2;

    /**
     * Number of pre-synthesised reference contours to retain in memory so
     * repeating the same sentence doesn't re-hit Piper / YIN.  Set to 0 to
     * disable memoisation entirely (useful for memory-constrained devices).
     */
    int reference_contour_cache_size = 8;

    /**
     * Number of weakest phonemes (per `LearningStore::weakest_phonemes`) the
     * picker tries to bias toward.  At each draw we flip a weighted coin:
     * with probability `weakness_bias` pick a sentence whose IPA plan
     * contains one of the weakest phonemes; otherwise round-robin.  Set
     * `weakness_bias = 0.0` to fall back to pure round-robin.
     */
    int weakest_phonemes_n = 3;
    double weakness_bias = 0.7;

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
 *      it, and stashes the reference pitch contour for scoring.
 *   2. `score(Utterance)` analyses the learner's PCM against the last
 *      announced reference and emits a `PronunciationFeedbackAction`.
 *
 * Internally this class is a thin coordinator over four collaborators
 * under `src/learning/pronunciation/drill/`:
 *
 *   - `DrillSentencePicker`     — pool + spaced-repetition picker
 *   - `DrillReferenceAudio`     — Piper synth + LRU cache + SDL replay
 *   - `DrillScoringPipeline`    — plan → infer → align → score → intonation
 *   - `DrillProgressLogger`     — JSON + `ProgressTracker` bridge
 *
 * When onnxruntime / espeak-ng / curriculum resources are missing the
 * processor gracefully returns an action with an explanatory reply
 * instead of crashing, mirroring how `EnglishTutorProcessor` handles
 * missing-API scenarios.
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
     * Emits a `PronunciationFeedbackAction`.
     */
    Action score(const std::vector<float>& user_pcm_16k, const std::string& transcript);

    /** Inject a pre-built phoneme model (tests use fake emissions). */
    void set_phoneme_model_for_test(std::unique_ptr<pronunciation::PhonemeModel> m);

    /** Inject a sentence list directly (tests skip file / DB loading). */
    void set_sentences_for_test(std::vector<std::string> s);

    /**
     * Prime the processor with a reference sentence, its pre-computed G2P plan,
     * and an optional reference pitch contour — skips the espeak-ng / Piper
     * calls that `pick_and_announce()` would otherwise make.  Intended only
     * for unit tests driving `score()` directly.
     */
    void set_reference_for_test(std::string reference,
                                pronunciation::G2PResult plan,
                                prosody::PitchContour reference_contour = {});

    /** Expose the model vocab for tests / external inspection. */
    const pronunciation::PhonemeVocab& vocab() const;

    /** Expose the user-side pitch tracker so callers can reuse its config. */
    const prosody::PitchTracker& pitch_tracker() const {
        return scoring_.user_pitch_tracker();
    }

private:
    const AppConfig& app_cfg_;
    LearningStore* store_;
    PronunciationDrillConfig cfg_;

    pronunciation::drill::DrillSentencePicker picker_;
    pronunciation::drill::DrillScoringPipeline scoring_;
    pronunciation::drill::DrillReferenceAudio reference_;
    pronunciation::drill::DrillProgressLogger progress_logger_;

    std::string current_reference_;
    prosody::PitchContour current_reference_contour_;

    // Test-only override for the G2P plan used inside `score()`.
    bool test_plan_set_ = false;
    pronunciation::G2PResult test_plan_;

    bool loaded_ = false;
};

} // namespace hecquin::learning
