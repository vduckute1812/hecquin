#include "learning/TextChunker.hpp"

#include <algorithm>
#include <cctype>

namespace hecquin::learning {

std::vector<std::string> chunk_text(const std::string& text, int chunk_chars, int overlap) {
    std::vector<std::string> out;
    if (text.empty()) return out;
    if (chunk_chars <= 0) chunk_chars = 1800;
    if (overlap < 0) overlap = 0;
    if (overlap >= chunk_chars) overlap = chunk_chars / 4;

    const size_t step = static_cast<size_t>(chunk_chars - overlap);
    for (size_t i = 0; i < text.size(); i += step) {
        size_t end = std::min(text.size(), i + static_cast<size_t>(chunk_chars));
        if (end < text.size()) {
            size_t soft = end;
            while (soft > i + static_cast<size_t>(chunk_chars / 2) &&
                   !std::isspace(static_cast<unsigned char>(text[soft]))) {
                --soft;
            }
            if (soft > i + static_cast<size_t>(chunk_chars / 2)) {
                end = soft;
            }
        }
        std::string piece = text.substr(i, end - i);
        size_t a = 0, b = piece.size();
        while (a < b && std::isspace(static_cast<unsigned char>(piece[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(piece[b - 1]))) --b;
        if (b > a) out.emplace_back(piece.substr(a, b - a));
        if (end >= text.size()) break;
    }
    return out;
}

} // namespace hecquin::learning
