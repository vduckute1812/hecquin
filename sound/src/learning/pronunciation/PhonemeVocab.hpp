#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace hecquin::learning::pronunciation {

/**
 * Bidirectional IPA ↔ integer-id map for a wav2vec2-phoneme model.
 *
 * Loaded from a HuggingFace-style `vocab.json` (single JSON object mapping
 * token strings to integer ids) or built programmatically for tests.  The
 * CTC blank is detected from common names (`"<pad>"`, `"<blank>"`, `"_"`).
 */
class PhonemeVocab {
public:
    PhonemeVocab() = default;

    /** Construct from a parsed (token → id) map.  Largest id + 1 defines vocab size. */
    static PhonemeVocab from_map(std::unordered_map<std::string, int> token_to_id);

    /** Load from a HuggingFace `vocab.json` on disk.  Returns `nullopt` on any parse error. */
    static std::optional<PhonemeVocab> load_json_file(const std::string& path);

    /** Parse vocab JSON text (so tests can feed strings directly). */
    static std::optional<PhonemeVocab> parse_json(const std::string& json_text);

    [[nodiscard]] int size() const { return static_cast<int>(id_to_token_.size()); }
    [[nodiscard]] int blank_id() const { return blank_id_; }

    /** Look up a token; returns -1 when unknown. */
    [[nodiscard]] int id_of(const std::string& token) const;

    /** Get the IPA string for id, or empty when out of range. */
    [[nodiscard]] const std::string& token_of(int id) const;

    /**
     * Tokenise a raw IPA string from espeak-ng into model-vocab tokens using
     * greedy longest-match over the vocab entries.  Unknown codepoints are
     * dropped (ids stay >= 0 for returned tokens).
     */
    [[nodiscard]] std::vector<int> tokenize_ipa(const std::string& ipa_stream) const;

private:
    void finalize_();

    std::unordered_map<std::string, int> token_to_id_;
    std::vector<std::string> id_to_token_;
    std::vector<std::size_t> lengths_desc_;  ///< unique byte-lengths, descending, for greedy match
    int blank_id_ = 0;
    std::string empty_;
};

} // namespace hecquin::learning::pronunciation
