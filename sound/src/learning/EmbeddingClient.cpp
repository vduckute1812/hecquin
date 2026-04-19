#include "learning/EmbeddingClient.hpp"

#include "ai/HttpClient.hpp"

#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace hecquin::learning {

namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    std::ostringstream oss;
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    out += oss.str();
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

/**
 * Finds the first `"embedding"` key that is followed by a JSON array of numbers
 * anywhere in `body`, and returns the parsed floats.  The OpenAI reply shape is:
 *   { "data": [ { "embedding": [ ... ], ... } ], ... }
 */
std::optional<std::vector<float>> extract_embedding_array(const std::string& body) {
    constexpr const char* kKey = "\"embedding\"";
    size_t pos = body.find(kKey);
    if (pos == std::string::npos) return std::nullopt;
    pos = body.find('[', pos + 11);
    if (pos == std::string::npos) return std::nullopt;
    ++pos;

    std::vector<float> out;
    std::string num;
    num.reserve(32);

    auto flush = [&]() {
        if (num.empty()) return;
        try {
            out.push_back(std::stof(num));
        } catch (...) {
        }
        num.clear();
    };

    for (; pos < body.size(); ++pos) {
        const char c = body[pos];
        if (c == ']') { flush(); break; }
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
            flush();
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' ||
            c == '.' || c == 'e' || c == 'E') {
            num.push_back(c);
        }
    }

    if (out.empty()) return std::nullopt;
    return out;
}

} // namespace

EmbeddingClient::EmbeddingClient(AiClientConfig config) : config_(std::move(config)) {}

bool EmbeddingClient::ready() const {
#ifdef HECQUIN_WITH_CURL
    return !config_.api_key.empty() && !config_.embeddings_url.empty();
#else
    return false;
#endif
}

std::optional<std::vector<float>> EmbeddingClient::embed(const std::string& text) const {
    if (!ready()) return std::nullopt;

    // gemini-embedding-001 natively returns 3072-dim vectors; the OpenAI-compat
    // endpoint honours a "dimensions" override so we can keep the configured
    // schema stable (e.g. 768) without re-provisioning the vec0 table.
    std::string body = std::string("{\"model\":\"") + json_escape(config_.embedding_model) +
                       "\",\"input\":\"" + json_escape(text) + "\"";
    if (config_.embedding_dim > 0) {
        body += ",\"dimensions\":" + std::to_string(config_.embedding_dim);
    }
    body += "}";

    const auto result = http_post_json(config_.embeddings_url, config_.api_key, body);
    if (!result) {
        std::cerr << "[EmbeddingClient] transport failure" << std::endl;
        return std::nullopt;
    }
    if (result->status < 200 || result->status >= 300) {
        std::cerr << "[EmbeddingClient] HTTP " << result->status << ": "
                  << result->body.substr(0, 500) << std::endl;
        return std::nullopt;
    }

    auto vec = extract_embedding_array(result->body);
    if (!vec) {
        std::cerr << "[EmbeddingClient] could not parse embedding from body: "
                  << result->body.substr(0, 200) << std::endl;
        return std::nullopt;
    }
    if (config_.embedding_dim > 0 && static_cast<int>(vec->size()) != config_.embedding_dim) {
        std::cerr << "[EmbeddingClient] dim mismatch (got " << vec->size()
                  << ", expected " << config_.embedding_dim << ")" << std::endl;
    }
    return vec;
}

std::optional<std::vector<std::vector<float>>>
EmbeddingClient::embed_batch(const std::vector<std::string>& texts) const {
    std::vector<std::vector<float>> out;
    out.reserve(texts.size());
    for (const auto& t : texts) {
        auto e = embed(t);
        if (!e) return std::nullopt;
        out.push_back(std::move(*e));
    }
    return out;
}

} // namespace hecquin::learning
