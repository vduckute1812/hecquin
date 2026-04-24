#include "learning/pronunciation/drill/DrillScoringPipeline.hpp"

#include <algorithm>
#include <iostream>
#include <utility>

namespace hecquin::learning::pronunciation::drill {

DrillScoringPipeline::DrillScoringPipeline(Config cfg)
    : cfg_(std::move(cfg)),
      phoneme_model_(std::make_unique<PhonemeModel>()),
      pitch_tracker_user_(prosody::PitchTrackerConfig{/*sr*/ 16000}) {}

bool DrillScoringPipeline::load() {
    const bool model_ok = phoneme_model_->load(cfg_.model_cfg);
    if (!model_ok) {
        std::cerr << "[PronunciationDrill] phoneme model unavailable — scoring will be degraded."
                  << std::endl;
    }
    g2p_ = std::make_unique<G2P>(phoneme_model_->vocab(), cfg_.g2p_opts);

    // Per-phoneme calibration: apply overrides from the optional JSON file
    // referenced by AppConfig.  Missing/malformed file is a no-op; the
    // scorer falls back to its global (min_logp, max_logp) anchors.
    if (!cfg_.calibration_path.empty()) {
        PronunciationScorerConfig scfg;
        if (scfg.load_calibration_json(cfg_.calibration_path)) {
            pron_scorer_ = PronunciationScorer(std::move(scfg));
            std::cout << "[PronunciationDrill] loaded calibration from "
                      << cfg_.calibration_path << std::endl;
        }
    }

    loaded_ = model_ok;
    return model_ok;
}

bool DrillScoringPipeline::available() const {
    return loaded_ && phoneme_model_ && phoneme_model_->available();
}

void DrillScoringPipeline::set_phoneme_model_for_test(std::unique_ptr<PhonemeModel> m) {
    phoneme_model_ = std::move(m);
    g2p_ = std::make_unique<G2P>(phoneme_model_->vocab());
    loaded_ = true;
}

const PhonemeVocab& DrillScoringPipeline::vocab() const {
    return phoneme_model_->vocab();
}

DrillScoringPipeline::Outcome
DrillScoringPipeline::run(const std::string& reference,
                          const std::string& transcript,
                          const std::vector<float>& user_pcm_16k,
                          const G2PResult* override_plan,
                          const prosody::PitchContour& reference_contour) {
    Outcome out;
    out.feedback.reference = reference;
    out.feedback.transcript = transcript;

    G2PResult plan;
    if (override_plan) {
        plan = *override_plan;
    } else {
        if (!g2p_) g2p_ = std::make_unique<G2P>(phoneme_model_->vocab());
        plan = g2p_->phonemize(reference);
    }

    Emissions emissions;
    if (phoneme_model_) {
        emissions = phoneme_model_->infer(user_pcm_16k);
    }

    AlignResult align;
    if (!plan.empty() && !emissions.logits.empty()) {
        align = aligner_.align(emissions, plan.flat_ids());
    }

    if (align.ok) {
        out.pron = pron_scorer_.score(plan, align);
    }

    if (!reference_contour.empty()) {
        const auto learner_contour = pitch_tracker_user_.track(user_pcm_16k);
        out.intonation = intonation_scorer_.score(reference_contour, learner_contour);
    } else {
        out.intonation.issues.emplace_back("No reference pitch contour — skipping intonation.");
    }

    out.feedback.pron_overall_0_100 = out.pron.overall_0_100;
    out.feedback.intonation_overall_0_100 = out.intonation.overall_0_100;
    out.feedback.intonation_issues = out.intonation.issues;

    // Rank words worst-first and keep the N lowest scorers (non-empty words only).
    std::vector<const WordScore*> ranked;
    ranked.reserve(out.pron.words.size());
    for (const auto& w : out.pron.words) {
        if (!w.phonemes.empty()) ranked.push_back(&w);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const WordScore* a, const WordScore* b) {
                  return a->score_0_100 < b->score_0_100;
              });
    const std::size_t feedback_cap =
        cfg_.max_feedback_words > 0
            ? static_cast<std::size_t>(cfg_.max_feedback_words)
            : 0;
    const std::size_t n_low = std::min<std::size_t>(ranked.size(), feedback_cap);
    for (std::size_t i = 0; i < n_low; ++i) {
        const auto& w = *ranked[i];
        PronunciationFeedbackAction::LowWord lw;
        lw.word = w.word;
        lw.score_0_100 = w.score_0_100;
        std::string ipa;
        ipa.reserve(w.phonemes.size() * 2);
        ipa.push_back('/');
        for (const auto& p : w.phonemes) ipa += p.ipa;
        ipa.push_back('/');
        lw.hint_ipa = std::move(ipa);
        out.feedback.lowest_words.push_back(std::move(lw));
    }

    return out;
}

} // namespace hecquin::learning::pronunciation::drill
