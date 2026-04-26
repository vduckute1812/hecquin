#include "ai/IHttpClient.hpp"
#include "config/ai/AiClientConfig.hpp"
#include "learning/EmbeddingClient.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

int fail(const char* message) {
    std::cerr << "[test_embedding_client_json] FAIL: " << message << std::endl;
    return 1;
}

class FakeHttp final : public hecquin::ai::IHttpClient {
public:
    HttpResult canned{200, ""};
    std::string last_body;

    std::optional<HttpResult> post_json(const std::string& /*url*/,
                                        const std::string& /*bearer*/,
                                        const std::string& json_body,
                                        long /*timeout*/) override {
        last_body = json_body;
        return canned;
    }
};

} // namespace

int main() {
    using hecquin::learning::EmbeddingClient;

    // --- parse_response ------------------------------------------------------
    {
        const std::string body =
            R"({"data":[{"embedding":[0.1,0.2,0.3]},{"embedding":[0.4,0.5,0.6]}]})";
        const auto got = EmbeddingClient::parse_response(body, 2, 3);
        if (!got || got->size() != 2) return fail("parse two vectors");
        if ((*got)[0].size() != 3 || (*got)[1].size() != 3) return fail("vector dims");
        if ((*got)[1][2] < 0.55f || (*got)[1][2] > 0.65f) return fail("vector values");
    }
    {
        // Wrong dim must reject.
        const std::string body = R"({"data":[{"embedding":[0.1,0.2]}]})";
        const auto got = EmbeddingClient::parse_response(body, 1, 3);
        if (got) return fail("dim mismatch should be nullopt");
    }
    {
        // Completely malformed.
        const auto got = EmbeddingClient::parse_response("<html>nope</html>", 1, 0);
        if (got) return fail("non-JSON should be nullopt");
    }
    {
        // Count mismatch.
        const std::string body = R"({"data":[{"embedding":[0.1]}]})";
        const auto got = EmbeddingClient::parse_response(body, 2, 1);
        if (got) return fail("wrong count should be nullopt");
    }

    // --- build_request_body --------------------------------------------------
    {
        const auto body = EmbeddingClient::build_request_body(
            "gemini-embedding-001", {"hello", "world"}, 8);
        if (body.find("\"model\":\"gemini-embedding-001\"") == std::string::npos) {
            return fail("body must contain model");
        }
        if (body.find("\"dimensions\":8") == std::string::npos) {
            return fail("body must contain dimensions");
        }
        if (body.find("\"hello\"") == std::string::npos ||
            body.find("\"world\"") == std::string::npos) {
            return fail("body must contain both inputs");
        }
    }

    // --- End-to-end through fake HTTP ---------------------------------------
    {
        FakeHttp fake;
        fake.canned = {200,
                       R"({"data":[{"embedding":[1.0,2.0,3.0]}]})"};

        AiClientConfig cfg;
        cfg.api_key = "fake";
        cfg.embeddings_url = "https://unit.test/embeddings";
        cfg.embedding_model = "fake-model";
        cfg.embedding_dim = 3;

        EmbeddingClient client(cfg, fake);
        const auto got = client.embed("hi");
        if (!got || got->size() != 3) return fail("fake end-to-end single");
        if ((*got)[0] < 0.9f) return fail("fake vector value");

        // Body sent to HTTP must contain "input":["hi"]
        if (fake.last_body.find("\"input\":[\"hi\"]") == std::string::npos) {
            return fail("sent body shape");
        }
    }

    // Non-2xx treated as failure.
    {
        FakeHttp fake;
        fake.canned = {500, "{\"error\":\"boom\"}"};

        AiClientConfig cfg;
        cfg.api_key = "fake";
        cfg.embeddings_url = "https://unit.test/embeddings";
        cfg.embedding_model = "fake-model";
        cfg.embedding_dim = 3;

        EmbeddingClient client(cfg, fake);
        if (client.embed("hi")) return fail("500 must yield nullopt");
    }

    return 0;
}
