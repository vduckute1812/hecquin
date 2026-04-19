#pragma once

#include "config/ai/AiClientConfig.hpp"

#include <optional>
#include <string>
#include <vector>

namespace hecquin::learning {

/**
 * POSTs text to an OpenAI-compatible `/embeddings` endpoint (tested against Gemini's
 * compatibility layer with `gemini-embedding-001`).  The reply body is parsed with a tiny
 * hand-rolled JSON scanner — no external library.
 */
class EmbeddingClient {
public:
    explicit EmbeddingClient(AiClientConfig config);

    bool ready() const;

    /** Embed a single string. Returns nullopt on transport/parse error. */
    std::optional<std::vector<float>> embed(const std::string& text) const;

    /** Batch helper — embeds one item per call, stops on first failure. */
    std::optional<std::vector<std::vector<float>>> embed_batch(const std::vector<std::string>& texts) const;

    const AiClientConfig& config() const { return config_; }

private:
    AiClientConfig config_;
};

} // namespace hecquin::learning
