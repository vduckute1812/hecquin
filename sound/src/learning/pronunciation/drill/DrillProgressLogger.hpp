#pragma once

#include "learning/pronunciation/PronunciationScorer.hpp"
#include "learning/prosody/IntonationScorer.hpp"

#include <string>

namespace hecquin::learning {
class ProgressTracker;
} // namespace hecquin::learning

namespace hecquin::learning::pronunciation::drill {

/**
 * Bridges scoring output to the persistent progress tracker.
 *
 * Owns the `build_phoneme_json` serialiser that used to live inside
 * `PronunciationDrillProcessor`.  `log()` is a no-op when no tracker
 * was supplied at construction — keeps tests from needing to open a
 * DB.
 */
class DrillProgressLogger {
public:
    explicit DrillProgressLogger(ProgressTracker* tracker);

    /**
     * Serialise the combined pronunciation + intonation scores as JSON
     * and emit them + per-phoneme rollups through the tracker.
     */
    void log(const std::string& reference,
             const std::string& transcript,
             const PronunciationScore& pron,
             const prosody::IntonationScore& intonation);

    /** Visible for tests; no external callers. */
    static std::string build_json(const PronunciationScore& pron,
                                  const prosody::IntonationScore& intonation);

private:
    ProgressTracker* tracker_;
};

} // namespace hecquin::learning::pronunciation::drill
