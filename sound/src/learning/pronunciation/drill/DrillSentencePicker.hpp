#pragma once

#include "learning/pronunciation/G2P.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace hecquin::learning {
class LearningStore;
} // namespace hecquin::learning

namespace hecquin::learning::pronunciation::drill {

/**
 * Spaced-repetition sentence picker.
 *
 * Owns:
 *   - the drill sentence pool (loaded from file / DB / fallback),
 *   - a phoneme → sentence-indices index built with a supplied G2P,
 *   - the picker policy that biases toward sentences containing the
 *     learner's weakest phonemes (as reported by `LearningStore`).
 *
 * The policy is pure round-robin when either `weakness_bias == 0`, the
 * store is unavailable, or the phoneme index is empty — so this class
 * works perfectly fine offline.
 */
class DrillSentencePicker {
public:
    struct Config {
        /** Number of weakest phonemes to pull from the store each draw. */
        int weakest_phonemes_n = 3;
        /** Probability of biasing toward a weak-phoneme sentence. */
        double weakness_bias = 0.7;
    };

    DrillSentencePicker(LearningStore* store, Config cfg);

    /**
     * Install a pool + build the phoneme index.  Pass `g2p = nullptr`
     * to skip the index — picker will round-robin.  Ownership of the
     * pool transfers in.
     */
    void load(std::vector<std::string> pool, G2P* g2p);

    /** Inject a pool directly without building the index (tests). */
    void set_pool(std::vector<std::string> pool);

    /** Pick the next sentence text.  Returns "" when the pool is empty. */
    std::string next();

    bool empty() const { return pool_.empty(); }
    std::size_t size() const { return pool_.size(); }
    const std::vector<std::string>& pool() const { return pool_; }

private:
    void build_phoneme_index_(G2P* g2p);
    std::size_t choose_weak_biased_index_();

    LearningStore* store_ = nullptr;
    Config cfg_;
    std::vector<std::string> pool_;
    std::size_t next_idx_ = 0;
    std::unordered_map<std::string, std::vector<std::size_t>> phoneme_to_sentences_;
};

} // namespace hecquin::learning::pronunciation::drill
