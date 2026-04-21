#include "learning/pronunciation/G2P.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <vector>

#ifndef ESPEAK_NG_EXECUTABLE
#define ESPEAK_NG_EXECUTABLE "espeak-ng"
#endif

namespace hecquin::learning::pronunciation {

namespace {

std::string shell_quote(const std::string& value) {
    std::string q;
    q.reserve(value.size() + 2);
    q.push_back('\'');
    for (char c : value) {
        if (c == '\'') q += "'\\''";
        else q.push_back(c);
    }
    q.push_back('\'');
    return q;
}

// Strip leading/trailing whitespace + simple ASCII punctuation from a word.
std::string clean_word(const std::string& in) {
    std::size_t first = 0, last = in.size();
    while (first < last) {
        const unsigned char c = static_cast<unsigned char>(in[first]);
        if (std::isalnum(c) || (c & 0x80)) break;   // keep non-ASCII (accents, etc.)
        ++first;
    }
    while (last > first) {
        const unsigned char c = static_cast<unsigned char>(in[last - 1]);
        if (std::isalnum(c) || (c & 0x80)) break;
        --last;
    }
    std::string out = in.substr(first, last - first);
    for (auto& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : text) {
        if (std::isspace(c)) {
            if (!cur.empty()) {
                auto cleaned = clean_word(cur);
                if (!cleaned.empty()) out.push_back(std::move(cleaned));
                cur.clear();
            }
        } else {
            cur.push_back(static_cast<char>(c));
        }
    }
    if (!cur.empty()) {
        auto cleaned = clean_word(cur);
        if (!cleaned.empty()) out.push_back(std::move(cleaned));
    }
    return out;
}

// Split an IPA stream on whitespace into per-word substreams.
std::vector<std::string> split_ipa_on_whitespace(const std::string& stream) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : stream) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            if (!cur.empty()) {
                out.push_back(std::move(cur));
                cur.clear();
            }
        } else {
            cur.push_back(static_cast<char>(c));
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

}  // namespace

G2P::G2P(const PhonemeVocab& vocab) : G2P(vocab, Options{}) {}

G2P::G2P(const PhonemeVocab& vocab, Options opts)
    : vocab_(vocab), opts_(std::move(opts)) {
    if (opts_.espeak_executable == "espeak-ng") {
        opts_.espeak_executable = ESPEAK_NG_EXECUTABLE;
    }
}

std::string G2P::run_espeak_(const std::string& text) const {
    // espeak-ng -q (no audio) -x (write phonemes) --ipa=3 (IPA with word separators)
    // -v <voice>
    const std::string cmd =
        shell_quote(opts_.espeak_executable) +
        " -q --ipa=3 -v " + shell_quote(opts_.voice) +
        " " + shell_quote(text) +
        " 2>/dev/null";

    std::array<char, 4096> buf{};
    std::string out;

    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) return {};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
        out.append(buf.data());
    }
    const int rc = ::pclose(pipe);
    if (rc != 0) return {};
    return out;
}

G2PResult G2P::tokenize_ipa_stream(const std::string& ipa_stream,
                                   const std::vector<std::string>& words) const {
    G2PResult result;
    const auto per_word = split_ipa_on_whitespace(ipa_stream);
    const std::size_t n = std::min(words.size(), per_word.size());

    for (std::size_t i = 0; i < n; ++i) {
        WordPhonemes w;
        w.word = words[i];
        const auto ids = vocab_.tokenize_ipa(per_word[i]);
        w.phonemes.reserve(ids.size());
        for (int id : ids) {
            PhonemeToken t;
            t.id = id;
            t.ipa = vocab_.token_of(id);
            w.phonemes.push_back(std::move(t));
        }
        result.words.push_back(std::move(w));
    }
    // If espeak returned fewer words than the source (punctuation collapses),
    // fall back to keeping the source surface words with empty phonemes so
    // downstream scoring can still line up words by index where possible.
    for (std::size_t i = n; i < words.size(); ++i) {
        WordPhonemes w;
        w.word = words[i];
        result.words.push_back(std::move(w));
    }
    return result;
}

G2PResult G2P::phonemize(const std::string& text) const {
    const auto words = split_words(text);
    if (words.empty()) return {};

    const std::string ipa = run_espeak_(text);
    if (ipa.empty()) {
        // espeak unavailable — return empty to signal upstream.
        return {};
    }
    return tokenize_ipa_stream(ipa, words);
}

} // namespace hecquin::learning::pronunciation
