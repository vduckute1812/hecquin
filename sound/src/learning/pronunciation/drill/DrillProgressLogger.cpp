#include "learning/pronunciation/drill/DrillProgressLogger.hpp"

#include "learning/ProgressTracker.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace hecquin::learning::pronunciation::drill {

DrillProgressLogger::DrillProgressLogger(ProgressTracker* tracker)
    : tracker_(tracker) {}

std::string DrillProgressLogger::build_json(const PronunciationScore& pron,
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
    // `replace` guards against the IPA / word text containing stray non-UTF-8
    // bytes (e.g. if a future vocab source feeds bad input through G2P).
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

void DrillProgressLogger::log(const std::string& reference,
                              const std::string& transcript,
                              const PronunciationScore& pron,
                              const prosody::IntonationScore& intonation) {
    if (!tracker_) return;
    std::vector<std::pair<std::string, float>> per_phoneme;
    per_phoneme.reserve(pron.words.size() * 4);
    for (const auto& w : pron.words) {
        for (const auto& p : w.phonemes) {
            per_phoneme.emplace_back(p.ipa, p.score_0_100);
        }
    }
    tracker_->log_pronunciation(reference,
                                transcript,
                                pron.overall_0_100,
                                intonation.overall_0_100,
                                build_json(pron, intonation),
                                per_phoneme);
}

} // namespace hecquin::learning::pronunciation::drill
