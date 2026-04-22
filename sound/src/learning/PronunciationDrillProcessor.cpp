#include "learning/PronunciationDrillProcessor.hpp"

#include "actions/PronunciationFeedbackAction.hpp"
#include "learning/store/LearningStore.hpp"
#include "learning/ProgressTracker.hpp"
#include "tts/PiperSpeech.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

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

std::string build_phoneme_json(const pronunciation::PronunciationScore& pron,
                               const prosody::IntonationScore& into) {
    nlohmann::json j;
    j["pron_overall"] = pron.overall_0_100;
    j["intonation_overall"] = into.overall_0_100;
    j["final_direction_match"] = into.final_direction_match;
    j["reference_direction"] = prosody::to_string(into.reference_direction);
    j["learner_direction"] = prosody::to_string(into.learner_direction);
    auto& words = j["words"] = nlohmann::json::array();
    for (const auto& w : pron.words) {
        nlohmann::json wj;
        wj["word"] = w.word;
        wj["score"] = w.score_0_100;
        auto& phonemes = wj["phonemes"] = nlohmann::json::array();
        for (const auto& p : w.phonemes) {
            phonemes.push_back({{"ipa", p.ipa}, {"score", p.score_0_100}});
        }
        words.push_back(std::move(wj));
    }
    auto& issues = j["issues"] = nlohmann::json::array();
    for (const auto& s : into.issues) issues.push_back(s);
    return j.dump();
}

}  // namespace

PronunciationDrillProcessor::PronunciationDrillProcessor(
    const AppConfig& app_cfg,
    LearningStore* store,
    ProgressTracker* progress,
    const std::string& piper_model_path,
    PronunciationDrillConfig cfg)
    : app_cfg_(app_cfg),
      store_(store),
      progress_(progress),
      piper_model_path_(piper_model_path),
      cfg_(std::move(cfg)),
      phoneme_model_(std::make_unique<pronunciation::PhonemeModel>()),
      pitch_tracker_user_(prosody::PitchTrackerConfig{/*sr*/ 16000}),
      pitch_tracker_piper_(prosody::PitchTrackerConfig{/*sr*/ 22050}) {}

PronunciationDrillProcessor::~PronunciationDrillProcessor() = default;

void PronunciationDrillProcessor::set_phoneme_model_for_test(
    std::unique_ptr<pronunciation::PhonemeModel> m) {
    phoneme_model_ = std::move(m);
    g2p_ = std::make_unique<pronunciation::G2P>(phoneme_model_->vocab());
    loaded_ = true;
}

void PronunciationDrillProcessor::set_sentences_for_test(std::vector<std::string> s) {
    sentence_pool_ = std::move(s);
}

const pronunciation::PhonemeVocab& PronunciationDrillProcessor::vocab() const {
    return phoneme_model_->vocab();
}

bool PronunciationDrillProcessor::load() {
    // Model + vocab.
    pronunciation::PhonemeModelConfig mc;
    mc.model_path = app_cfg_.pronunciation.model_path;
    mc.vocab_path = app_cfg_.pronunciation.vocab_path;
    mc.provider = app_cfg_.pronunciation.onnx_provider;

    const bool model_ok = phoneme_model_->load(mc);
    if (!model_ok) {
        std::cerr << "[PronunciationDrill] phoneme model unavailable — scoring will be degraded."
                  << std::endl;
    }
    g2p_ = std::make_unique<pronunciation::G2P>(phoneme_model_->vocab());

    // Sentences: try config file first, then DB documents(kind='drill'), then fallback.
    if (sentence_pool_.empty()) {
        if (!app_cfg_.pronunciation.drill_sentences_path.empty()) {
            sentence_pool_ = load_sentences_from_file(app_cfg_.pronunciation.drill_sentences_path);
        }
    }
    if (sentence_pool_.empty() && store_) {
        sentence_pool_ = store_->sample_drill_sentences(50);
    }
    if (sentence_pool_.empty()) {
        sentence_pool_ = cfg_.fallback_sentences;
    }

    // Randomise order so sessions do not always start with the same sentence.
    std::random_device rd;
    std::mt19937 rng(rd());
    std::shuffle(sentence_pool_.begin(), sentence_pool_.end(), rng);

    loaded_ = model_ok;
    return model_ok;
}

bool PronunciationDrillProcessor::available() const {
    return loaded_ && phoneme_model_ && phoneme_model_->available();
}

std::string PronunciationDrillProcessor::next_sentence_() {
    if (sentence_pool_.empty()) return {};
    const std::string& s = sentence_pool_[next_sentence_idx_ % sentence_pool_.size()];
    ++next_sentence_idx_;
    return s;
}

std::vector<float> PronunciationDrillProcessor::int16_to_float_(
    const std::vector<int16_t>& in) {
    std::vector<float> out(in.size());
    constexpr float kInvMax = 1.0f / 32768.0f;
    for (std::size_t i = 0; i < in.size(); ++i) {
        out[i] = static_cast<float>(in[i]) * kInvMax;
    }
    return out;
}

void PronunciationDrillProcessor::compute_reference_pitch_(const std::string& text) {
    current_reference_contour_ = {};
    std::vector<int16_t> samples;
    int sr = 0;
    if (!piper_synthesize_to_buffer(text, piper_model_path_, samples, sr)) {
        std::cerr << "[PronunciationDrill] Piper failed to synthesise reference." << std::endl;
        return;
    }
    const auto floats = int16_to_float_(samples);

    prosody::PitchTrackerConfig pcfg;
    pcfg.sample_rate = sr > 0 ? sr : 22050;
    pcfg.frame_hop_samples = pcfg.sample_rate / 100;   // 10 ms
    pcfg.frame_size_samples = std::max(512, pcfg.sample_rate / 16);
    pitch_tracker_piper_ = prosody::PitchTracker(pcfg);
    current_reference_contour_ = pitch_tracker_piper_.track(floats);

    // Replay the audio so the learner hears the target.  This uses the same
    // int16 path as piper_speak_and_play, but avoids regenerating.
    sdl_play_s16_mono_22k(samples);
}

std::string PronunciationDrillProcessor::pick_and_announce() {
    current_reference_ = next_sentence_();
    if (current_reference_.empty()) return {};
    std::cout << "🎯 Reference: " << current_reference_ << std::endl;
    compute_reference_pitch_(current_reference_);
    return current_reference_;
}

Action PronunciationDrillProcessor::score(
    const std::vector<float>& user_pcm_16k,
    const std::string& transcript) {

    PronunciationFeedbackAction fb;
    fb.reference = current_reference_;
    fb.transcript = transcript;

    if (current_reference_.empty()) {
        Action a;
        a.kind = ActionKind::PronunciationFeedback;
        a.reply = "I have not picked a sentence yet. Say \"start pronunciation drill\" to begin.";
        a.transcript = transcript;
        return a;
    }

    if (!g2p_) g2p_ = std::make_unique<pronunciation::G2P>(phoneme_model_->vocab());
    const auto plan = g2p_->phonemize(current_reference_);

    pronunciation::Emissions emissions;
    if (phoneme_model_) {
        emissions = phoneme_model_->infer(user_pcm_16k);
    }

    pronunciation::AlignResult align;
    if (!plan.empty() && !emissions.logits.empty()) {
        align = aligner_.align(emissions, plan.flat_ids());
    }

    pronunciation::PronunciationScore pron;
    if (align.ok) {
        pron = pron_scorer_.score(plan, align, emissions.frame_stride_ms);
    }

    // Intonation: YIN on the learner PCM at 16 kHz.
    prosody::IntonationScore into;
    if (!current_reference_contour_.empty()) {
        const auto learner_contour = pitch_tracker_user_.track(user_pcm_16k);
        into = intonation_scorer_.score(current_reference_contour_, learner_contour);
    } else {
        into.issues.emplace_back("No reference pitch contour — skipping intonation.");
    }

    fb.pron_overall_0_100 = pron.overall_0_100;
    fb.intonation_overall_0_100 = into.overall_0_100;
    fb.intonation_issues = into.issues;

    // Rank words worst-first and keep the 2 lowest scorers (non-empty words only).
    std::vector<const pronunciation::WordScore*> ranked;
    ranked.reserve(pron.words.size());
    for (const auto& w : pron.words) {
        if (!w.phonemes.empty()) ranked.push_back(&w);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const pronunciation::WordScore* a, const pronunciation::WordScore* b) {
                  return a->score_0_100 < b->score_0_100;
              });
    const std::size_t n_low = std::min<std::size_t>(ranked.size(), 2);
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
        fb.lowest_words.push_back(std::move(lw));
    }

    // Persistence.
    if (progress_) {
        std::vector<std::pair<std::string, float>> per_phoneme;
        per_phoneme.reserve(align.segments.size());
        for (const auto& w : pron.words) {
            for (const auto& p : w.phonemes) {
                per_phoneme.emplace_back(p.ipa, p.score_0_100);
            }
        }
        progress_->log_pronunciation(current_reference_,
                                     transcript,
                                     fb.pron_overall_0_100,
                                     fb.intonation_overall_0_100,
                                     build_phoneme_json(pron, into),
                                     per_phoneme);
    }

    return fb.into_action(transcript);
}

} // namespace hecquin::learning
