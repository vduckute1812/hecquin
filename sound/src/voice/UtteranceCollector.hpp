#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

class AudioCapture;
struct VoiceListenerConfig;

namespace hecquin::voice {

/** Raw stats + PCM for one end-of-utterance event, ready for secondary-gate /
 *  Whisper. */
struct CollectedUtterance {
    std::vector<float> pcm;
    int voiced_frames = 0;
    int total_frames = 0;
    int speech_ms = 0;
    int silence_ms = 0;
};

/**
 * Poll-loop collector.  Owns the 50 ms poll cadence, primary VAD,
 * collection timers, and voiced-ratio counters that used to live in
 * `VoiceListener::run()`.  Each call to `collect_next()` blocks until
 * either:
 *   - an utterance with `speech_ms ≥ min_speech_ms` and a trailing
 *     `silence_ms ≥ end_silence_ms` is observed, or
 *   - `app_running` goes false (returns `std::nullopt`).
 *
 * Separating this out lets the listener test the collection loop in
 * isolation, and keeps `VoiceListener` focused on dispatch +
 * observability.
 */
class UtteranceCollector {
public:
    UtteranceCollector(AudioCapture& capture,
                       const VoiceListenerConfig& cfg,
                       const std::atomic<bool>& app_running);

    std::optional<CollectedUtterance> collect_next();

    /** RMS over [start, end).  Exposed so the listener can run the
     *  same calculation over the full utterance buffer. */
    static float rms(const std::vector<float>& samples,
                     std::size_t start, std::size_t end);

private:
    bool voice_active_(const std::vector<float>& samples) const;

    AudioCapture& capture_;
    const VoiceListenerConfig& cfg_;
    const std::atomic<bool>& app_running_;
};

} // namespace hecquin::voice
