#include "learning/pronunciation/PhonemeVocab.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace hecquin::learning::pronunciation {

namespace {

constexpr const char* kDefaultBlankTokens[] = {"<pad>", "<blank>", "_", "|"};

}  // namespace

PhonemeVocab PhonemeVocab::from_map(std::unordered_map<std::string, int> token_to_id) {
    PhonemeVocab v;
    v.token_to_id_ = std::move(token_to_id);
    v.finalize_();
    return v;
}

std::optional<PhonemeVocab> PhonemeVocab::parse_json(const std::string& json_text) {
    try {
        const auto j = nlohmann::json::parse(json_text, /*cb*/ nullptr,
                                             /*allow exc*/ false,
                                             /*ignore comments*/ true);
        if (!j.is_object()) return std::nullopt;
        std::unordered_map<std::string, int> m;
        m.reserve(j.size());
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!it.value().is_number_integer()) continue;
            m.emplace(it.key(), it.value().get<int>());
        }
        if (m.empty()) return std::nullopt;
        return from_map(std::move(m));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<PhonemeVocab> PhonemeVocab::load_json_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    std::ostringstream buf;
    buf << in.rdbuf();
    return parse_json(buf.str());
}

void PhonemeVocab::finalize_() {
    int max_id = -1;
    for (const auto& [tok, id] : token_to_id_) {
        if (id > max_id) max_id = id;
    }
    id_to_token_.assign(static_cast<std::size_t>(max_id + 1), std::string{});
    for (const auto& [tok, id] : token_to_id_) {
        if (id >= 0 && id < static_cast<int>(id_to_token_.size())) {
            id_to_token_[static_cast<std::size_t>(id)] = tok;
        }
    }

    blank_id_ = 0;
    for (const char* cand : kDefaultBlankTokens) {
        auto it = token_to_id_.find(cand);
        if (it != token_to_id_.end()) {
            blank_id_ = it->second;
            break;
        }
    }

    // Unique byte-lengths, largest first — greedy longest-match pre-index.
    std::set<std::size_t, std::greater<>> lens;
    for (const auto& [tok, _] : token_to_id_) {
        if (!tok.empty()) lens.insert(tok.size());
    }
    lengths_desc_.assign(lens.begin(), lens.end());
}

int PhonemeVocab::id_of(const std::string& token) const {
    auto it = token_to_id_.find(token);
    return it == token_to_id_.end() ? -1 : it->second;
}

const std::string& PhonemeVocab::token_of(int id) const {
    if (id < 0 || id >= static_cast<int>(id_to_token_.size())) return empty_;
    return id_to_token_[static_cast<std::size_t>(id)];
}

namespace {

// espeak-ng IPA includes stress marks (ˈ ˌ) and length marks (ː) that are
// rarely themselves model tokens; skip them when the vocab does not list them.
bool is_skippable_codepoint(const std::string& stream, std::size_t pos, std::size_t& next_pos) {
    static const char* kSkip[] = {
        "ˈ", "ˌ", "‖", "|", ".", "ː",
    };
    for (const char* m : kSkip) {
        const std::size_t len = std::strlen(m);
        if (pos + len <= stream.size() && stream.compare(pos, len, m) == 0) {
            next_pos = pos + len;
            return true;
        }
    }
    return false;
}

// Advance one UTF-8 codepoint from `pos`; returns the new offset.
std::size_t next_codepoint(const std::string& s, std::size_t pos) {
    if (pos >= s.size()) return pos;
    const unsigned char c = static_cast<unsigned char>(s[pos]);
    if      ((c & 0x80) == 0x00) return pos + 1;
    else if ((c & 0xE0) == 0xC0) return pos + 2;
    else if ((c & 0xF0) == 0xE0) return pos + 3;
    else if ((c & 0xF8) == 0xF0) return pos + 4;
    return pos + 1;   // malformed; advance one byte
}

}  // namespace

std::vector<int> PhonemeVocab::tokenize_ipa(const std::string& ipa_stream) const {
    std::vector<int> out;
    std::size_t i = 0;
    while (i < ipa_stream.size()) {
        if (std::isspace(static_cast<unsigned char>(ipa_stream[i]))) {
            ++i;
            continue;
        }
        std::size_t skip_to = 0;
        if (is_skippable_codepoint(ipa_stream, i, skip_to)) {
            // If the skip marker *is* a vocab token (e.g. "ː"), prefer the token.
            const std::string tok = ipa_stream.substr(i, skip_to - i);
            auto it = token_to_id_.find(tok);
            if (it != token_to_id_.end()) {
                out.push_back(it->second);
            }
            i = skip_to;
            continue;
        }

        bool matched = false;
        for (std::size_t len : lengths_desc_) {
            if (i + len > ipa_stream.size()) continue;
            auto it = token_to_id_.find(ipa_stream.substr(i, len));
            if (it != token_to_id_.end()) {
                out.push_back(it->second);
                i += len;
                matched = true;
                break;
            }
        }
        if (!matched) {
            i = next_codepoint(ipa_stream, i);
        }
    }
    return out;
}

} // namespace hecquin::learning::pronunciation
