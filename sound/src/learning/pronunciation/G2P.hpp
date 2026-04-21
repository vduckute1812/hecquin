#pragma once

#include "learning/pronunciation/PhonemeTypes.hpp"
#include "learning/pronunciation/PhonemeVocab.hpp"

#include <string>

namespace hecquin::learning::pronunciation {

/**
 * Grapheme → phoneme conversion via `espeak-ng --ipa=3`.
 *
 * We shell out so the module does not need the espeak-ng C API headers
 * (Piper already pulls in the runtime; reusing the binary keeps the build
 * graph flat).  The raw IPA stream is then greedily tokenised against the
 * supplied `PhonemeVocab` so output ids are directly feedable to a wav2vec2
 * phoneme model.
 */
class G2P {
public:
    struct Options {
        std::string espeak_executable;
        std::string voice;
        int timeout_ms = 4000;

        Options() : espeak_executable("espeak-ng"), voice("en-us") {}
    };

    explicit G2P(const PhonemeVocab& vocab);
    G2P(const PhonemeVocab& vocab, Options opts);

    /**
     * Phonemize `text`.  Whitespace in the input delimits words; punctuation
     * is stripped.  On any failure (espeak missing, nonzero exit) the result
     * is empty.
     */
    [[nodiscard]] G2PResult phonemize(const std::string& text) const;

    /**
     * Internal helper exposed for tests: take a pre-generated IPA stream
     * (e.g. `"ɡˈʊd mˈɔːɹnɪŋ"`) and tokenise it into words.  Spaces delimit
     * words; non-vocab codepoints are dropped.
     */
    [[nodiscard]] G2PResult tokenize_ipa_stream(const std::string& ipa_stream,
                                                const std::vector<std::string>& words) const;

private:
    [[nodiscard]] std::string run_espeak_(const std::string& text) const;

    const PhonemeVocab& vocab_;
    Options opts_;
};

} // namespace hecquin::learning::pronunciation
