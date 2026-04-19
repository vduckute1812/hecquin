#pragma once

#include "config/ai/AiClientConfig.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hecquin::ai {
class IHttpClient;
class CurlHttpClient;
}

namespace hecquin::learning {

/**
 * POSTs text to an OpenAI-compatible `/embeddings` endpoint (tested against Gemini's
 * compatibility layer with `gemini-embedding-001`).  Request bodies are built and
 * response bodies are parsed with `nlohmann::json`.
 *
 * Default-constructed uses a libcurl-backed `IHttpClient`.  A second
 * constructor accepts an external client so tests can inject canned HTTP
 * responses without a network round-trip.
 */
class EmbeddingClient {
public:
    explicit EmbeddingClient(AiClientConfig config);
    EmbeddingClient(AiClientConfig config, hecquin::ai::IHttpClient& http);
    ~EmbeddingClient();

    EmbeddingClient(const EmbeddingClient&) = delete;
    EmbeddingClient& operator=(const EmbeddingClient&) = delete;

    bool ready() const;

    /** Embed a single string. Returns nullopt on transport/parse error. */
    std::optional<std::vector<float>> embed(const std::string& text) const;

    /**
     * Embed many strings in one HTTP round-trip (uses the OpenAI-compatible
     * array `input` form).  The result preserves input order; `nullopt` on any
     * transport, HTTP, or parse failure.
     */
    std::optional<std::vector<std::vector<float>>>
    embed_many(const std::vector<std::string>& texts) const;

    /**
     * Convenience: splits `texts` into HTTP batches of at most `batch_size`
     * and concatenates the vectors.  `batch_size <= 0` falls back to a single
     * request.  Returns `nullopt` on first failure.
     */
    std::optional<std::vector<std::vector<float>>>
    embed_batch(const std::vector<std::string>& texts, int batch_size = 32) const;

    const AiClientConfig& config() const { return config_; }

    /**
     * Build the request body (model + `input` array + optional `dimensions`).
     * Exposed so tests can round-trip a request through a fake client.
     */
    static std::string build_request_body(const std::string& model,
                                          const std::vector<std::string>& texts,
                                          int embedding_dim);

    /**
     * Parse an `/embeddings` JSON response.  If `expected_dim` > 0 each vector
     * must match that length.  Returns `nullopt` on parse / dim / length error.
     */
    static std::optional<std::vector<std::vector<float>>>
    parse_response(const std::string& body, size_t expected_count, int expected_dim);

private:
    AiClientConfig                                     config_;
    std::unique_ptr<hecquin::ai::CurlHttpClient>       owned_http_;
    hecquin::ai::IHttpClient*                          http_ = nullptr;
};

} // namespace hecquin::learning
