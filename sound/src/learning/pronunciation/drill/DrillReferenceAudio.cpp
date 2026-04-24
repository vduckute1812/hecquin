#include "learning/pronunciation/drill/DrillReferenceAudio.hpp"

#include "tts/PiperSpeech.hpp"

#include <algorithm>
#include <iostream>

namespace hecquin::learning::pronunciation::drill {

DrillReferenceAudio::DrillReferenceAudio(Config cfg)
    : cfg_(std::move(cfg)),
      piper_tracker_(prosody::PitchTrackerConfig{/*sr*/ 22050}) {}

std::vector<float> DrillReferenceAudio::int16_to_float(const std::vector<std::int16_t>& in) {
    std::vector<float> out(in.size());
    constexpr float kInvMax = 1.0f / 32768.0f;
    for (std::size_t i = 0; i < in.size(); ++i) {
        out[i] = static_cast<float>(in[i]) * kInvMax;
    }
    return out;
}

const DrillReferenceAudio::Entry*
DrillReferenceAudio::cache_lookup_(const std::string& text) {
    if (cfg_.cache_size <= 0) return nullptr;
    auto it = lru_map_.find(text);
    if (it == lru_map_.end()) return nullptr;
    // MRU bump: move the key to the front of the list.
    lru_keys_.erase(it->second.second);
    lru_keys_.push_front(text);
    it->second.second = lru_keys_.begin();
    return &it->second.first;
}

void DrillReferenceAudio::cache_put_(const std::string& text, Entry entry) {
    if (cfg_.cache_size <= 0) return;
    lru_keys_.push_front(text);
    lru_map_.emplace(text, std::make_pair(std::move(entry), lru_keys_.begin()));
    while (static_cast<int>(lru_keys_.size()) > cfg_.cache_size) {
        const std::string& victim = lru_keys_.back();
        lru_map_.erase(victim);
        lru_keys_.pop_back();
    }
}

prosody::PitchContour DrillReferenceAudio::announce(const std::string& text) {
    if (const Entry* hit = cache_lookup_(text)) {
        // Replay the cached PCM so the learner still hears the target,
        // and reuse the pre-computed contour.  Restoring the tracker's
        // config keeps any sample-rate-dependent tuning in sync.
        piper_tracker_ = prosody::PitchTracker(hit->tracker_cfg);
        const auto contour = hit->contour;
        sdl_play_s16_mono_22k(hit->pcm);
        return contour;
    }

    std::vector<std::int16_t> samples;
    int sr = 0;
    if (!piper_synthesize_to_buffer(text, cfg_.piper_model_path, samples, sr)) {
        std::cerr << "[PronunciationDrill] Piper failed to synthesise reference." << std::endl;
        return {};
    }
    const auto floats = int16_to_float(samples);

    prosody::PitchTrackerConfig pcfg;
    pcfg.sample_rate = sr > 0 ? sr : 22050;
    pcfg.frame_hop_samples = pcfg.sample_rate / 100;   // 10 ms
    pcfg.frame_size_samples = std::max(512, pcfg.sample_rate / 16);
    piper_tracker_ = prosody::PitchTracker(pcfg);
    prosody::PitchContour contour = piper_tracker_.track(floats);

    // Populate the cache *before* playback so a crash during SDL does
    // not prevent the next-attempt shortcut.
    Entry entry;
    entry.contour = contour;
    entry.pcm = samples;
    entry.sample_rate = pcfg.sample_rate;
    entry.tracker_cfg = pcfg;
    cache_put_(text, std::move(entry));

    // Replay through SDL — reuses the same int16 path as piper_speak_and_play
    // but avoids re-synthesising.
    sdl_play_s16_mono_22k(samples);
    return contour;
}

void DrillReferenceAudio::put_for_test(const std::string& text,
                                       prosody::PitchContour contour,
                                       std::vector<std::int16_t> pcm,
                                       int sample_rate) {
    Entry entry;
    entry.contour = std::move(contour);
    entry.pcm = std::move(pcm);
    entry.sample_rate = sample_rate;
    prosody::PitchTrackerConfig pcfg;
    pcfg.sample_rate = sample_rate > 0 ? sample_rate : 22050;
    entry.tracker_cfg = pcfg;
    cache_put_(text, std::move(entry));
}

const prosody::PitchContour*
DrillReferenceAudio::lookup_for_test(const std::string& text) {
    if (const Entry* hit = cache_lookup_(text)) return &hit->contour;
    return nullptr;
}

std::vector<std::string> DrillReferenceAudio::cache_keys_for_test() const {
    return {lru_keys_.begin(), lru_keys_.end()};
}

} // namespace hecquin::learning::pronunciation::drill
