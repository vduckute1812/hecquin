#include "learning/pronunciation/drill/DrillSentencePicker.hpp"

#include "learning/store/LearningStore.hpp"

#include <random>
#include <utility>

namespace hecquin::learning::pronunciation::drill {

DrillSentencePicker::DrillSentencePicker(LearningStore* store, Config cfg)
    : store_(store), cfg_(cfg) {}

void DrillSentencePicker::load(std::vector<std::string> pool, G2P* g2p) {
    pool_ = std::move(pool);
    next_idx_ = 0;
    build_phoneme_index_(g2p);
}

void DrillSentencePicker::set_pool(std::vector<std::string> pool) {
    pool_ = std::move(pool);
    next_idx_ = 0;
    phoneme_to_sentences_.clear();
}

void DrillSentencePicker::build_phoneme_index_(G2P* g2p) {
    phoneme_to_sentences_.clear();
    if (!g2p || pool_.empty()) return;
    phoneme_to_sentences_.reserve(128);
    for (std::size_t i = 0; i < pool_.size(); ++i) {
        const auto plan = g2p->phonemize(pool_[i]);
        if (plan.empty()) continue;
        // De-dupe per-sentence so a single sentence doesn't get listed
        // multiple times for a phoneme that happens to repeat in it.
        std::unordered_map<std::string, bool> seen;
        for (const auto& w : plan.words) {
            for (const auto& p : w.phonemes) {
                if (p.ipa.empty()) continue;
                if (seen.emplace(p.ipa, true).second) {
                    phoneme_to_sentences_[p.ipa].push_back(i);
                }
            }
        }
    }
}

std::size_t DrillSentencePicker::choose_weak_biased_index_() {
    if (pool_.empty()) return 0;

    // Early-out to pure round-robin when the picker is disabled or we have
    // no mastery data / no phoneme index to work with.
    if (!store_ || cfg_.weakness_bias <= 0.0 ||
        cfg_.weakest_phonemes_n <= 0 || phoneme_to_sentences_.empty()) {
        return next_idx_++ % pool_.size();
    }

    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> coin(0.0, 1.0);
    if (coin(rng) >= cfg_.weakness_bias) {
        return next_idx_++ % pool_.size();
    }

    const auto weak = store_->weakest_phonemes(cfg_.weakest_phonemes_n);
    std::vector<std::size_t> candidates;
    candidates.reserve(16);
    for (const auto& ipa : weak) {
        auto it = phoneme_to_sentences_.find(ipa);
        if (it == phoneme_to_sentences_.end()) continue;
        for (auto idx : it->second) candidates.push_back(idx);
    }
    if (candidates.empty()) {
        return next_idx_++ % pool_.size();
    }
    std::uniform_int_distribution<std::size_t> pick(0, candidates.size() - 1);
    return candidates[pick(rng)];
}

std::string DrillSentencePicker::next() {
    if (pool_.empty()) return {};
    const std::size_t idx = choose_weak_biased_index_();
    return pool_[idx];
}

} // namespace hecquin::learning::pronunciation::drill
