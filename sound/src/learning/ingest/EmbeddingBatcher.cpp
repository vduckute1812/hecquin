#include "learning/ingest/EmbeddingBatcher.hpp"

#include "learning/EmbeddingClient.hpp"

#include <algorithm>
#include <utility>

namespace hecquin::learning::ingest {

EmbeddingBatcher::EmbeddingBatcher(EmbeddingClient& embed, int batch_size)
    : embed_(embed), batch_size_(std::max(1, batch_size)) {}

std::vector<std::optional<std::vector<float>>>
EmbeddingBatcher::embed_slice(const std::vector<std::string>& slice) {
    std::vector<std::optional<std::vector<float>>> out;
    out.reserve(slice.size());

    const auto batch_embed = embed_.embed_many(slice);
    if (batch_embed && batch_embed->size() == slice.size()) {
        for (auto& v : *batch_embed) {
            out.emplace_back(std::move(v));
        }
        return out;
    }

    // Fallback: per-chunk so a single bad chunk doesn't tank the file.
    for (const auto& chunk : slice) {
        auto single = embed_.embed(chunk);
        out.emplace_back(single ? std::optional<std::vector<float>>(std::move(*single))
                                : std::nullopt);
    }
    return out;
}

} // namespace hecquin::learning::ingest
