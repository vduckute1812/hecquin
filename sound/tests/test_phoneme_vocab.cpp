#include "learning/pronunciation/PhonemeVocab.hpp"

#include <iostream>
#include <string>

using hecquin::learning::pronunciation::PhonemeVocab;

namespace {

int fail(const char* msg) {
    std::cerr << "[test_phoneme_vocab] FAIL: " << msg << std::endl;
    return 1;
}

}  // namespace

int main() {
    // Minimal wav2vec2-style vocab covering a few English IPA tokens.
    const std::string json = R"({
        "<pad>": 0,
        "<unk>": 1,
        " ": 2,
        "θ": 3,
        "ɹ": 4,
        "u": 5,
        "ː": 6,
        "ɡ": 7,
        "ʊ": 8,
        "d": 9,
        "m": 10,
        "ɔ": 11,
        "n": 12,
        "ɪ": 13,
        "ŋ": 14
    })";

    auto vocab_opt = PhonemeVocab::parse_json(json);
    if (!vocab_opt) return fail("parse_json failed");
    const PhonemeVocab& v = *vocab_opt;

    if (v.blank_id() != 0) return fail("blank id should be the <pad> entry");
    if (v.id_of("θ") < 0) return fail("θ should be known");
    if (v.token_of(v.id_of("θ")) != "θ") return fail("round-trip id_of/token_of");
    if (v.id_of("not-a-real-token") >= 0) return fail("unknown token should return -1");

    // "through" roughly transcribes to θ ɹ u ː — four single-codepoint tokens.
    const auto through_ids = v.tokenize_ipa("θɹuː");
    if (through_ids.size() != 4)
        return fail("θɹuː should produce 4 phonemes (θ ɹ u ː)");
    if (v.token_of(through_ids[0]) != "θ") return fail("first is θ");
    if (v.token_of(through_ids[1]) != "ɹ") return fail("second is ɹ");
    if (v.token_of(through_ids[2]) != "u") return fail("third is u");
    if (v.token_of(through_ids[3]) != "ː") return fail("fourth is ː");

    // Stress marks (not in vocab) should be dropped without eating following phonemes.
    const auto stressed = v.tokenize_ipa("ˈθɹuː");
    if (stressed != through_ids)
        return fail("leading stress mark must not change token output");

    // Whitespace is just a separator.
    const auto spaced = v.tokenize_ipa(" θɹ uː ");
    if (spaced != through_ids)
        return fail("whitespace should not affect tokenization");

    // "good morning" → ɡ ʊ d m ɔ ː ɹ n ɪ ŋ (10 tokens).
    const auto gm_ids = v.tokenize_ipa("ɡʊdmɔːɹnɪŋ");
    if (gm_ids.size() != 10)
        return fail("good morning should give 10 tokens");

    // Fully unknown input produces no tokens (codepoints silently skipped).
    const auto none = v.tokenize_ipa("x̃ỹ");
    if (!none.empty()) return fail("unknown IPA should yield no tokens");

    std::cout << "[test_phoneme_vocab] OK" << std::endl;
    return 0;
}
