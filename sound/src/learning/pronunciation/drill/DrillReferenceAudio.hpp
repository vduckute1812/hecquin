#pragma once

#include "learning/prosody/PitchTracker.hpp"

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace hecquin::learning::pronunciation::drill {

/**
 * Reference-audio collaborator.
 *
 * Responsible for:
 *   - synthesising reference audio via Piper for a target sentence,
 *   - computing its pitch contour with YIN at Piper's native rate,
 *   - caching (PCM, contour) for fast repeat announcements,
 *   - playing the PCM back through SDL so the learner hears the target.
 *
 * Split out of `PronunciationDrillProcessor` so the caching + replay
 * policy has one home — mocking Piper in tests is now just "construct
 * with an empty model path and call `set_for_test()`".
 */
class DrillReferenceAudio {
public:
    struct Config {
        std::string piper_model_path;
        /** Number of entries to retain.  0 disables the cache. */
        int cache_size = 8;
    };

    explicit DrillReferenceAudio(Config cfg);

    /**
     * Synthesise the reference for `text`, play it back, and return the
     * pitch contour.  The contour is empty on synthesis failure — the
     * caller should treat that as "skip intonation scoring".
     */
    prosody::PitchContour announce(const std::string& text);

    /** Expose the internal Piper-rate tracker (config reuse by tests). */
    const prosody::PitchTracker& piper_tracker() const { return piper_tracker_; }

    // Test-only hooks.  Populate the cache directly (no Piper, no SDL)
    // so the LRU policy can be verified offline.
    void put_for_test(const std::string& text,
                      prosody::PitchContour contour,
                      std::vector<std::int16_t> pcm,
                      int sample_rate);

    /** Returns the contour if `text` is present (also bumps LRU recency). */
    const prosody::PitchContour* lookup_for_test(const std::string& text);

    /** Current number of cached entries. */
    std::size_t cache_size_for_test() const { return lru_keys_.size(); }

    /** Iteration order: MRU → LRU. */
    std::vector<std::string> cache_keys_for_test() const;

private:
    struct Entry {
        prosody::PitchContour contour;
        std::vector<std::int16_t> pcm;
        int sample_rate = 0;
        prosody::PitchTrackerConfig tracker_cfg;
    };

    static std::vector<float> int16_to_float(const std::vector<std::int16_t>& in);

    void cache_put_(const std::string& text, Entry entry);
    const Entry* cache_lookup_(const std::string& text);

    Config cfg_;
    prosody::PitchTracker piper_tracker_;
    std::list<std::string> lru_keys_;
    std::unordered_map<std::string,
        std::pair<Entry, std::list<std::string>::iterator>> lru_map_;
};

} // namespace hecquin::learning::pronunciation::drill
