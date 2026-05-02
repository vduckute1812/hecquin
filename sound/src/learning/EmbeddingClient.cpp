#include "learning/EmbeddingClient.hpp"

#include "ai/IHttpClient.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

namespace hecquin::learning {

using nlohmann::json;

namespace {

// Retry transport blips and 408/425/429/5xx; backoff = 500/1000/2000/4000 ms.
constexpr int kMaxAttempts = 4;
constexpr int kBackoffBaseMs = 500;

StatusKind classify(long status) {
    if (status >= 200 && status < 300) return StatusKind::Ok;
    if (status == 400 || status == 401 || status == 403) return StatusKind::Stable;
    if (status == 408 || status == 425 || status == 429) return StatusKind::Retryable;
    if (status >= 500 && status < 600) return StatusKind::Retryable;
    return StatusKind::Other;
}

} // namespace

EmbeddingClient::EmbeddingClient(AiClientConfig config)
    : config_(std::move(config)),
      owned_http_(std::make_unique<hecquin::ai::CurlHttpClient>()),
      http_(owned_http_.get()) {}

EmbeddingClient::EmbeddingClient(AiClientConfig config, hecquin::ai::IHttpClient& http)
    : config_(std::move(config)),
      owned_http_(nullptr),
      http_(&http) {}

EmbeddingClient::~EmbeddingClient() = default;

bool EmbeddingClient::ready() const {
#ifdef HECQUIN_WITH_CURL
    return http_ != nullptr && !config_.api_key.empty() && !config_.embeddings_url.empty();
#else
    return http_ != nullptr && owned_http_ == nullptr &&
           !config_.api_key.empty() && !config_.embeddings_url.empty();
#endif
}

std::string EmbeddingClient::build_request_body(const std::string& model,
                                                const std::vector<std::string>& texts,
                                                int embedding_dim) {
    json body = {
        {"model", model},
        {"input", texts},
    };
    if (embedding_dim > 0) {
        // OpenAI-compat `dimensions` keeps the vec0 schema stable across providers.
        body["dimensions"] = embedding_dim;
    }
    // U+FFFD-replace invalid UTF-8 instead of throwing (ingest sanitizes upstream).
    return body.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::optional<std::vector<std::vector<float>>>
EmbeddingClient::parse_response(const std::string& body, size_t expected_count, int expected_dim) {
    std::vector<std::vector<float>> out;
    out.reserve(expected_count);
    try {
        const json parsed = json::parse(body);
        const auto& data = parsed.at("data");
        if (!data.is_array()) return std::nullopt;
        for (const auto& row : data) {
            const auto& emb = row.at("embedding");
            if (!emb.is_array()) return std::nullopt;
            std::vector<float> vec;
            vec.reserve(emb.size());
            for (const auto& x : emb) vec.push_back(static_cast<float>(x.get<double>()));
            if (expected_dim > 0 && static_cast<int>(vec.size()) != expected_dim) {
                return std::nullopt;
            }
            out.push_back(std::move(vec));
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (expected_count > 0 && out.size() != expected_count) return std::nullopt;
    return out;
}

std::optional<std::vector<float>> EmbeddingClient::embed(const std::string& text) const {
    auto many = embed_many({text});
    if (!many || many->empty()) return std::nullopt;
    return std::move((*many)[0]);
}

std::optional<std::vector<std::vector<float>>>
EmbeddingClient::embed_many(const std::vector<std::string>& texts) const {
    return embed_many_classified(texts).vectors;
}

std::optional<hecquin::ai::HttpResult>
EmbeddingClient::attempt_with_backoff_(const std::string& body) const {
    std::optional<hecquin::ai::HttpResult> result;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        result = http_->post_json(config_.embeddings_url, config_.api_key, body);

        if (result.has_value() && classify(result->status) != StatusKind::Retryable) {
            break;
        }

        const bool last = (attempt == kMaxAttempts);
        const int backoff_ms = kBackoffBaseMs * (1 << (attempt - 1));
        std::cerr << "[EmbeddingClient] "
                  << (result ? ("HTTP " + std::to_string(result->status))
                             : std::string("transport failure"))
                  << " on attempt " << attempt << "/" << kMaxAttempts;
        if (!last) {
            std::cerr << ", retrying in " << backoff_ms << "ms" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        } else {
            std::cerr << ", giving up" << std::endl;
        }
    }
    return result;
}

void EmbeddingClient::classify_http_status_(const hecquin::ai::HttpResult& result,
                                            EmbedManyResult& out) const {
    out.http_status = result.status;
    const auto kind = classify(result.status);
    if (kind == StatusKind::Ok) return;
    std::cerr << "[EmbeddingClient] HTTP " << result.status << ": "
              << result.body.substr(0, 500) << std::endl;
    if (kind == StatusKind::Stable) {
        out.retry_per_chunk_worthwhile = false;
    }
}

void EmbeddingClient::detect_dim_mismatch_in_(const std::string& body,
                                              EmbedManyResult& out) const {
    if (config_.embedding_dim <= 0) return;
    try {
        const auto j = json::parse(body);
        const auto& data = j.at("data");
        if (!data.is_array() || data.empty()) return;
        const auto& emb = data.front().at("embedding");
        if (emb.is_array() && static_cast<int>(emb.size()) != config_.embedding_dim) {
            out.dim_mismatch = true;
            out.retry_per_chunk_worthwhile = false;
        }
    } catch (...) {
        // Leave retry_per_chunk_worthwhile=true; a single bad chunk may parse alone.
    }
}

EmbedManyResult
EmbeddingClient::embed_many_classified(const std::vector<std::string>& texts) const {
    EmbedManyResult out;
    if (!ready() || texts.empty()) return out;

    const std::string body =
        build_request_body(config_.embedding_model, texts, config_.embedding_dim);
    auto result = attempt_with_backoff_(body);
    if (!result) return out;

    classify_http_status_(*result, out);
    if (classify(result->status) != StatusKind::Ok) return out;

    auto parsed = parse_response(result->body, texts.size(), config_.embedding_dim);
    if (!parsed) {
        std::cerr << "[EmbeddingClient] could not parse response\n  body: "
                  << result->body.substr(0, 200) << std::endl;
        detect_dim_mismatch_in_(result->body, out);
        return out;
    }
    out.vectors = std::move(parsed);
    return out;
}

std::optional<std::vector<std::vector<float>>>
EmbeddingClient::embed_batch(const std::vector<std::string>& texts, int batch_size) const {
    if (texts.empty()) return std::vector<std::vector<float>>{};
    if (batch_size <= 0 || static_cast<int>(texts.size()) <= batch_size) {
        return embed_many(texts);
    }

    std::vector<std::vector<float>> out;
    out.reserve(texts.size());
    for (size_t i = 0; i < texts.size(); i += static_cast<size_t>(batch_size)) {
        const size_t end = std::min(texts.size(), i + static_cast<size_t>(batch_size));
        std::vector<std::string> slice(texts.begin() + i, texts.begin() + end);
        auto part = embed_many(slice);
        if (!part) return std::nullopt;
        for (auto& v : *part) out.push_back(std::move(v));
    }
    return out;
}

} // namespace hecquin::learning
