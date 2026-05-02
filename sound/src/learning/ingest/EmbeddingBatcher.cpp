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

    const auto batch = embed_.embed_many_classified(slice);
    if (batch.vectors && batch.vectors->size() == slice.size()) {
        for (auto& v : *batch.vectors) {
            out.emplace_back(std::move(v));
        }
        return out;
    }

    // Stable failure: per-chunk would just re-burn quota with the same answer.
    if (!batch.retry_per_chunk_worthwhile) {
        out.assign(slice.size(), std::nullopt);
        return out;
    }

    for (const auto& chunk : slice) {
        auto single = embed_.embed(chunk);
        out.emplace_back(single ? std::optional<std::vector<float>>(std::move(*single))
                                : std::nullopt);
    }
    return out;
}

} // namespace hecquin::learning::ingest
