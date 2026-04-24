#include "learning/pronunciation/PronunciationScorer.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>

namespace hecquin::learning::pronunciation {

bool PronunciationScorerConfig::load_calibration_json(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    try {
        nlohmann::json j;
        in >> j;
        if (!j.is_object()) return false;
        // Schema: {"phonemes": {"<ipa>": {"min_logp": <float>, "max_logp": <float>}, …}}
        // Bare top-level form {"<ipa>": {...}} is also accepted for convenience.
        const auto& root = j.contains("phonemes") ? j["phonemes"] : j;
        if (!root.is_object()) return false;
        for (auto it = root.begin(); it != root.end(); ++it) {
            const auto& v = it.value();
            if (!v.is_object()) continue;
            PhonemeCalibration cal;
            cal.min_logp = v.value("min_logp", min_logp);
            cal.max_logp = v.value("max_logp", max_logp);
            if (cal.max_logp <= cal.min_logp) continue;
            per_phoneme[it.key()] = cal;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[PronunciationScorer] calibration JSON parse failed: "
                  << e.what() << std::endl;
        return false;
    }
}

PronunciationScorer::PronunciationScorer(PronunciationScorerConfig cfg)
    : cfg_(std::move(cfg)) {}

float PronunciationScorer::logp_to_score(float logp) const {
    if (cfg_.max_logp <= cfg_.min_logp) return 0.0f;
    const float clamped = std::clamp(logp, cfg_.min_logp, cfg_.max_logp);
    const float t = (clamped - cfg_.min_logp) / (cfg_.max_logp - cfg_.min_logp);
    return t * 100.0f;
}

float PronunciationScorer::logp_to_score(float logp, const std::string& ipa) const {
    if (!ipa.empty()) {
        auto it = cfg_.per_phoneme.find(ipa);
        if (it != cfg_.per_phoneme.end() && it->second.max_logp > it->second.min_logp) {
            const float clamped = std::clamp(logp, it->second.min_logp, it->second.max_logp);
            const float t = (clamped - it->second.min_logp)
                          / (it->second.max_logp - it->second.min_logp);
            return t * 100.0f;
        }
    }
    return logp_to_score(logp);
}

PronunciationScore PronunciationScorer::score(const G2PResult& plan,
                                              const AlignResult& alignment) const {
    PronunciationScore out;
    if (!alignment.ok || plan.empty()) return out;

    std::size_t expected = 0;
    for (const auto& w : plan.words) expected += w.phonemes.size();
    if (alignment.segments.size() != expected) return out;

    std::size_t seg_idx = 0;
    double total_sum = 0.0;
    std::size_t total_count = 0;

    for (const auto& w : plan.words) {
        WordScore ws;
        ws.word = w.word;
        if (w.phonemes.empty()) {
            out.words.push_back(std::move(ws));
            continue;
        }

        double word_sum = 0.0;
        std::size_t word_count = 0;
        std::size_t w_start = alignment.segments[seg_idx].start_frame;
        std::size_t w_end = w_start;

        for (const auto& pt : w.phonemes) {
            const auto& seg = alignment.segments[seg_idx++];
            PhonemeScore ps;
            ps.ipa = pt.ipa.empty() ? std::string{} : pt.ipa;
            ps.start_frame = seg.start_frame;
            ps.end_frame = seg.end_frame;
            ps.score_0_100 = logp_to_score(seg.log_posterior, ps.ipa);
            word_sum += ps.score_0_100;
            ++word_count;
            w_end = std::max(w_end, seg.end_frame);
            ws.phonemes.push_back(std::move(ps));
        }
        ws.start_frame = w_start;
        ws.end_frame = w_end;
        ws.score_0_100 = word_count > 0
            ? static_cast<float>(word_sum / static_cast<double>(word_count))
            : 0.0f;
        total_sum += word_sum;
        total_count += word_count;
        out.words.push_back(std::move(ws));
    }

    out.overall_0_100 = total_count > 0
        ? static_cast<float>(total_sum / static_cast<double>(total_count))
        : 0.0f;
    return out;
}

} // namespace hecquin::learning::pronunciation
