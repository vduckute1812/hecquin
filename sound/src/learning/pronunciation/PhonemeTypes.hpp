#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace hecquin::learning::pronunciation {

/**
 * One integer-valued phoneme id (index into the model vocab) tagged with its
 * printable IPA form.  Keeping both avoids threading the vocab through every
 * layer of the scoring pipeline.
 */
struct PhonemeToken {
    int id = -1;                ///< model vocab index; -1 when unknown / blank
    std::string ipa;            ///< IPA / espeak token, e.g. "θ", "ɹ", "uː"
};

/** Phoneme sequence for a single whitespace-delimited word. */
struct WordPhonemes {
    std::string word;                      ///< surface form, e.g. "through"
    std::vector<PhonemeToken> phonemes;    ///< target phonemes in order
};

/** Full grapheme-to-phoneme result for an utterance. */
struct G2PResult {
    std::vector<WordPhonemes> words;

    /** Flat phoneme-id sequence across all words (word boundaries dropped). */
    [[nodiscard]] std::vector<int> flat_ids() const {
        std::vector<int> out;
        for (const auto& w : words) {
            for (const auto& p : w.phonemes) out.push_back(p.id);
        }
        return out;
    }
    /** True when at least one phoneme was produced. */
    [[nodiscard]] bool empty() const {
        for (const auto& w : words) if (!w.phonemes.empty()) return false;
        return true;
    }
};

/**
 * Emission matrix from a CTC-style phoneme model.
 *
 * `logits[t]` holds log-probabilities for frame `t`, one entry per vocab id.
 * `frame_stride_ms` is the acoustic stride of the network (≈20 ms for
 * wav2vec2).  `blank_id` is the CTC blank token index (usually 0).
 */
struct Emissions {
    std::vector<std::vector<float>> logits;
    float frame_stride_ms = 20.0f;
    int blank_id = 0;

    [[nodiscard]] std::size_t num_frames() const { return logits.size(); }
    [[nodiscard]] std::size_t vocab_size() const { return logits.empty() ? 0 : logits.front().size(); }
};

/** One aligned phoneme span, in frame indices, with a raw log-posterior score. */
struct AlignSegment {
    int phoneme_id = -1;
    std::size_t start_frame = 0;
    std::size_t end_frame = 0;    ///< exclusive
    float log_posterior = 0.0f;   ///< mean log-prob of the target across [start,end)
};

/** Result of running forced alignment of a target sequence over an emissions matrix. */
struct AlignResult {
    std::vector<AlignSegment> segments;
    bool ok = false;

    [[nodiscard]] bool empty() const { return segments.empty(); }
};

/** Per-phoneme score (0..100) with the raw probabilities kept for inspection. */
struct PhonemeScore {
    std::string ipa;
    float score_0_100 = 0.0f;
    std::size_t start_frame = 0;
    std::size_t end_frame = 0;
};

struct WordScore {
    std::string word;
    float score_0_100 = 0.0f;
    std::size_t start_frame = 0;
    std::size_t end_frame = 0;
    std::vector<PhonemeScore> phonemes;
};

struct PronunciationScore {
    float overall_0_100 = 0.0f;
    std::vector<WordScore> words;

    [[nodiscard]] bool empty() const { return words.empty(); }
};

} // namespace hecquin::learning::pronunciation
