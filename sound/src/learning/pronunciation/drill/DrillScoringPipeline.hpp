#pragma once

#include "actions/PronunciationFeedbackAction.hpp"
#include "learning/pronunciation/CtcAligner.hpp"
#include "learning/pronunciation/G2P.hpp"
#include "learning/pronunciation/PhonemeModel.hpp"
#include "learning/pronunciation/PronunciationScorer.hpp"
#include "learning/prosody/IntonationScorer.hpp"
#include "learning/prosody/PitchTracker.hpp"

#include <memory>
#include <string>
#include <vector>

namespace hecquin::learning::pronunciation::drill {

/**
 * Scoring Template Method.
 *
 * The `run()` method applies the fixed skeleton
 *     phonemize → infer emissions → forced-align → GOP score → intonation
 * returning a populated `PronunciationFeedbackAction` plus the raw
 * `PronunciationScore` / `IntonationScore` so a progress logger can
 * persist them.  Individual steps are pure functions on the stored
 * collaborators — the method orders them, handles missing-plan /
 * missing-emissions / missing-contour edge cases, and keeps the
 * feedback-word cap invariant.
 *
 * Tests inject fakes via `set_phoneme_model_for_test` /
 * `set_g2p_for_test` instead of running onnxruntime / espeak-ng.
 */
class DrillScoringPipeline {
public:
    struct Config {
        PhonemeModelConfig model_cfg;
        G2P::Options g2p_opts;
        std::string calibration_path;
        int max_feedback_words = 2;
    };

    struct Outcome {
        PronunciationFeedbackAction feedback;
        PronunciationScore pron;
        prosody::IntonationScore intonation;
    };

    explicit DrillScoringPipeline(Config cfg);

    /** Load the phoneme model + construct the G2P.  Returns whether
     *  scoring will be available (false when model / onnx is missing). */
    bool load();

    /** Whether scoring will succeed (model + vocab usable). */
    [[nodiscard]] bool available() const;

    /** Inject a pre-built phoneme model (tests use fake emissions). */
    void set_phoneme_model_for_test(std::unique_ptr<PhonemeModel> m);

    /** Expose the model vocab for tests / external inspection. */
    const PhonemeVocab& vocab() const;

    /** Expose the user-rate pitch tracker config for reuse. */
    const prosody::PitchTracker& user_pitch_tracker() const { return pitch_tracker_user_; }

    /**
     * Run the full scoring skeleton.  Pass `override_plan` when a caller
     * has the G2P plan already (tests supply one via `set_reference_for_test`);
     * otherwise the pipeline invokes its own G2P.  An empty
     * `reference_contour` causes intonation scoring to be skipped with a
     * "no reference pitch contour" issue recorded on the outcome.
     */
    Outcome run(const std::string& reference,
                const std::string& transcript,
                const std::vector<float>& user_pcm_16k,
                const G2PResult* override_plan,
                const prosody::PitchContour& reference_contour);

private:
    Config cfg_;
    std::unique_ptr<PhonemeModel> phoneme_model_;
    std::unique_ptr<G2P> g2p_;
    CtcAligner aligner_;
    PronunciationScorer pron_scorer_;
    prosody::PitchTracker pitch_tracker_user_;    // 16 kHz (capture rate)
    prosody::IntonationScorer intonation_scorer_;
    bool loaded_ = false;
};

} // namespace hecquin::learning::pronunciation::drill
