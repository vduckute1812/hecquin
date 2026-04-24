#include "learning/Vocabulary.hpp"

#include <cctype>

namespace hecquin::learning::Vocabulary {

std::string normalise(const std::string& word) {
    std::string out;
    out.reserve(word.size());
    for (unsigned char c : word) {
        if (std::isalpha(c) || c == '\'') {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return out;
}

} // namespace hecquin::learning::Vocabulary
