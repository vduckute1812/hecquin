#include "learning/PronunciationDrillProcessor.hpp"

#include "actions/PronunciationFeedbackAction.hpp"
#include "learning/ProgressTracker.hpp"
#include "learning/store/LearningStore.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <utility>

namespace hecquin::learning {

namespace {

std::vector<std::string> load_sentences_from_file(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream in(path);
    if (!in) return out;
    std::string line;
    while (std::getline(in, line)) {
        // Strip BOM + trailing CR / whitespace.
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                 line.back() == '\t' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') continue;
        out.push_back(std::move(line));
        line.clear();
    }
    return out;
}

pronunciation::drill::DrillScoringPipeline::Config
make_scoring_cfg(const AppConfig& app_cfg, const PronunciationDrillConfig& cfg) {
    pronunciation::drill::DrillScoringPipeline::Config scfg;
    scfg.model_cfg.model_path = app_cfg.pronunciation.model_path;
    scfg.model_cfg.vocab_path = app_cfg.pronunciation.vocab_path;
    scfg.model_cfg.provider = app_cfg.pronunciation.onnx_provider;
    if (!app_cfg.locale.espeak_voice.empty()) {
        scfg.g2p_opts.voice = app_cfg.locale.espeak_voice;
    }
    scfg.calibration_path = app_cfg.pronunciation.calibration_path;
    scfg.max_feedback_words = cfg.max_feedback_words;
    return scfg;
}

pronunciation::drill::DrillReferenceAudio::Config
make_reference_cfg(const std::string& piper_model_path, const PronunciationDrillConfig& cfg) {
    pronunciation::drill::DrillReferenceAudio::Config rcfg;
    rcfg.piper_model_path = piper_model_path;
    rcfg.cache_size = cfg.reference_contour_cache_size;
    return rcfg;
}

pronunciation::drill::DrillSentencePicker::Config
make_picker_cfg(const PronunciationDrillConfig& cfg) {
    pronunciation::drill::DrillSentencePicker::Config pcfg;
    pcfg.weakest_phonemes_n = cfg.weakest_phonemes_n;
    pcfg.weakness_bias = cfg.weakness_bias;
    return pcfg;
}

} // namespace

PronunciationDrillProcessor::PronunciationDrillProcessor(
    const AppConfig& app_cfg,
    LearningStore* store,
    ProgressTracker* progress,
    const std::string& piper_model_path,
    PronunciationDrillConfig cfg)
    : app_cfg_(app_cfg),
      store_(store),
      cfg_(std::move(cfg)),
      picker_(store, make_picker_cfg(cfg_)),
      scoring_(make_scoring_cfg(app_cfg_, cfg_)),
      reference_(make_reference_cfg(piper_model_path, cfg_)),
      progress_logger_(progress) {}

PronunciationDrillProcessor::~PronunciationDrillProcessor() = default;

void PronunciationDrillProcessor::set_phoneme_model_for_test(
    std::unique_ptr<pronunciation::PhonemeModel> m) {
    scoring_.set_phoneme_model_for_test(std::move(m));
    loaded_ = true;
}

void PronunciationDrillProcessor::set_sentences_for_test(std::vector<std::string> s) {
    picker_.set_pool(std::move(s));
}

void PronunciationDrillProcessor::set_reference_for_test(
    std::string reference,
    pronunciation::G2PResult plan,
    prosody::PitchContour reference_contour) {
    current_reference_ = std::move(reference);
    test_plan_ = std::move(plan);
    test_plan_set_ = true;
    current_reference_contour_ = std::move(reference_contour);
}

const pronunciation::PhonemeVocab& PronunciationDrillProcessor::vocab() const {
    return scoring_.vocab();
}

bool PronunciationDrillProcessor::load() {
    const bool model_ok = scoring_.load();

    // Sentences: try config file first, then DB documents(kind='drill'), then fallback.
    std::vector<std::string> pool;
    if (!picker_.empty()) {
        // Pool was pre-injected (tests) — keep it as-is.
        loaded_ = model_ok;
        return model_ok;
    }
    if (!app_cfg_.pronunciation.drill_sentences_path.empty()) {
        pool = load_sentences_from_file(app_cfg_.pronunciation.drill_sentences_path);
    }
    if (pool.empty() && store_) {
        pool = store_->sample_drill_sentences(50);
    }
    if (pool.empty()) {
        pool = cfg_.fallback_sentences;
    }

    // Randomise order so sessions do not always start with the same sentence.
    std::random_device rd;
    std::mt19937 rng(rd());
    std::shuffle(pool.begin(), pool.end(), rng);

    // The picker's G2P reference is the one the scoring pipeline owns —
    // we cannot access it directly, so we phonemize via a temporary G2P
    // built against the same vocab.  This matches the pre-refactor
    // behaviour where the index was built off a fresh G2P under `load()`.
    pronunciation::G2P index_g2p(scoring_.vocab());
    picker_.load(std::move(pool), &index_g2p);

    loaded_ = model_ok;
    return model_ok;
}

bool PronunciationDrillProcessor::available() const {
    return loaded_ && scoring_.available();
}

std::string PronunciationDrillProcessor::pick_and_announce() {
    current_reference_ = picker_.next();
    if (current_reference_.empty()) return {};
    std::cout << "🎯 Reference: " << current_reference_ << std::endl;
    current_reference_contour_ = reference_.announce(current_reference_);
    return current_reference_;
}

Action PronunciationDrillProcessor::score(
    const std::vector<float>& user_pcm_16k,
    const std::string& transcript) {

    if (current_reference_.empty()) {
        Action a;
        a.kind = ActionKind::PronunciationFeedback;
        a.reply = "I have not picked a sentence yet. Say \"start pronunciation drill\" to begin.";
        a.transcript = transcript;
        return a;
    }

    const pronunciation::G2PResult* plan_override = test_plan_set_ ? &test_plan_ : nullptr;
    auto outcome = scoring_.run(current_reference_,
                                transcript,
                                user_pcm_16k,
                                plan_override,
                                current_reference_contour_);

    progress_logger_.log(current_reference_,
                         transcript,
                         outcome.pron,
                         outcome.intonation);

    return outcome.feedback.into_action(transcript);
}

} // namespace hecquin::learning
