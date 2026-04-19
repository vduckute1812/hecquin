#include "learning/EmbeddingClient.hpp"

#include "ai/IHttpClient.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>

namespace hecquin::learning {

using nlohmann::json;

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
        // gemini-embedding-001 natively returns 3072-dim vectors; the OpenAI
        // layer honours `dimensions` so we can keep the vec0 schema stable.
        body["dimensions"] = embedding_dim;
    }
    return body.dump();
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
    if (!ready() || texts.empty()) return std::nullopt;

    const std::string body =
        build_request_body(config_.embedding_model, texts, config_.embedding_dim);

    const auto result = http_->post_json(config_.embeddings_url, config_.api_key, body);
    if (!result) {
        std::cerr << "[EmbeddingClient] transport failure" << std::endl;
        return std::nullopt;
    }
    if (result->status < 200 || result->status >= 300) {
        std::cerr << "[EmbeddingClient] HTTP " << result->status << ": "
                  << result->body.substr(0, 500) << std::endl;
        return std::nullopt;
    }

    auto parsed = parse_response(result->body, texts.size(), config_.embedding_dim);
    if (!parsed) {
        std::cerr << "[EmbeddingClient] could not parse response\n  body: "
                  << result->body.substr(0, 200) << std::endl;
        return std::nullopt;
    }
    return parsed;
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
