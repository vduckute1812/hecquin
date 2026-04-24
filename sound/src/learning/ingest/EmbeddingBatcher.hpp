#pragma once

#include <optional>
#include <string>
#include <vector>

namespace hecquin::learning {
class EmbeddingClient;
} // namespace hecquin::learning

namespace hecquin::learning::ingest {

/**
 * Wraps `EmbeddingClient::embed_many` with the per-chunk fallback the
 * ingestor has always relied on: when the batched call fails (or
 * returns a wrong-sized array), re-issue one request per chunk so a
 * single bad chunk doesn't tank the whole file.
 *
 * Returns one entry per chunk in the input slice — `std::nullopt` for
 * chunks that still failed after the fallback.
 */
class EmbeddingBatcher {
public:
    EmbeddingBatcher(EmbeddingClient& embed, int batch_size);

    std::vector<std::optional<std::vector<float>>>
    embed_slice(const std::vector<std::string>& slice);

    int batch_size() const { return batch_size_; }

private:
    EmbeddingClient& embed_;
    int batch_size_;
};

} // namespace hecquin::learning::ingest
