#pragma once

#include "config/ai/AiClientConfig.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct HttpResult;

namespace hecquin::ai {
class IHttpClient;
class CurlHttpClient;
}

namespace hecquin::learning {

/**
 * Outcome of an `embed_many` round-trip. `vectors` is nullopt on any failure;
 * `retry_per_chunk_worthwhile` is false for *stable* failures (auth / bad
 * request / dim mismatch) so callers can skip a futile per-chunk fallback;
 * `dim_mismatch` lets the run hard-stop with a clear remediation message.
 */
struct EmbedManyResult {
    std::optional<std::vector<std::vector<float>>> vectors;
    long http_status = 0;
    bool retry_per_chunk_worthwhile = true;
    bool dim_mismatch = false;
};

/** HTTP status family used by the embedding retry / classification logic. */
enum class StatusKind {
    Ok,         // 2xx
    Stable,     // 400/401/403 — never retry
    Retryable,  // 408/425/429/5xx
    Other,      // 3xx / 404 / unknown — break, don't retry
};

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

    /** One HTTP round-trip; equivalent to `embed_many_classified(texts).vectors`. */
    std::optional<std::vector<std::vector<float>>>
    embed_many(const std::vector<std::string>& texts) const;

    /** Same as `embed_many` but with failure classification for retry decisions. */
    EmbedManyResult embed_many_classified(const std::vector<std::string>& texts) const;

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
    /** Retry loop. nullopt = transport failure on every attempt. */
    std::optional<::HttpResult> attempt_with_backoff_(const std::string& body) const;

    /** Sets http_status + retry_per_chunk_worthwhile from a transport-OK response. */
    void classify_http_status_(const ::HttpResult& result, EmbedManyResult& out) const;

    /** On parse failure, probe whether the body's `embedding` array is wrong-sized. */
    void detect_dim_mismatch_in_(const std::string& body, EmbedManyResult& out) const;

    AiClientConfig                                     config_;
    std::unique_ptr<hecquin::ai::CurlHttpClient>       owned_http_;
    hecquin::ai::IHttpClient*                          http_ = nullptr;
};

} // namespace hecquin::learning
