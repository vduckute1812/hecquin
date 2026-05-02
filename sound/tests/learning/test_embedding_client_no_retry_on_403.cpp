// Regression for issue #6: a stable failure (401/403/400) must not be
// retried — the API will give the same answer 4 times in a row and burn
// quota.  Test counts the number of POST attempts a fake HTTP client sees.

#include "ai/IHttpClient.hpp"
#include "config/ai/AiClientConfig.hpp"
#include "learning/EmbeddingClient.hpp"

#include <iostream>
#include <optional>
#include <string>

namespace {

using hecquin::ai::HttpResult;

int fail(const char* message) {
    std::cerr << "[test_embedding_client_no_retry_on_403] FAIL: " << message << std::endl;
    return 1;
}

class CountingHttp final : public hecquin::ai::IHttpClient {
public:
    HttpResult canned{200, ""};
    int call_count = 0;

    std::optional<HttpResult> post_json(const std::string& /*url*/,
                                        const std::string& /*bearer*/,
                                        const std::string& /*json_body*/,
                                        long /*timeout*/) override {
        ++call_count;
        return canned;
    }
};

AiClientConfig make_cfg() {
    AiClientConfig cfg;
    cfg.api_key = "fake";
    cfg.embeddings_url = "https://unit.test/embeddings";
    cfg.embedding_model = "fake-model";
    cfg.embedding_dim = 3;
    return cfg;
}

} // namespace

int main() {
    using hecquin::learning::EmbeddingClient;

    // 403 is stable: exactly one attempt, retry_per_chunk_worthwhile=false.
    {
        CountingHttp http;
        http.canned = {403, R"({"error":"forbidden"})"};
        EmbeddingClient client(make_cfg(), http);
        const auto result = client.embed_many_classified({"hello"});
        if (result.vectors) return fail("403 must yield no vectors");
        if (result.http_status != 403) return fail("403 status must be reported");
        if (result.retry_per_chunk_worthwhile)
            return fail("403 must mark retry-per-chunk as not worthwhile");
        if (http.call_count != 1) {
            std::cerr << "  observed " << http.call_count << " calls\n";
            return fail("403 must trigger exactly one HTTP call");
        }
    }

    // 401 is stable too.
    {
        CountingHttp http;
        http.canned = {401, R"({"error":"unauthenticated"})"};
        EmbeddingClient client(make_cfg(), http);
        const auto result = client.embed_many_classified({"hello"});
        if (result.retry_per_chunk_worthwhile)
            return fail("401 must mark retry-per-chunk as not worthwhile");
        if (http.call_count != 1) return fail("401 must trigger one HTTP call");
    }

    // 400 is stable too.
    {
        CountingHttp http;
        http.canned = {400, R"({"error":"bad request"})"};
        EmbeddingClient client(make_cfg(), http);
        const auto result = client.embed_many_classified({"hello"});
        if (result.retry_per_chunk_worthwhile)
            return fail("400 must mark retry-per-chunk as not worthwhile");
        if (http.call_count != 1) return fail("400 must trigger one HTTP call");
    }

    // Retryable status (5xx) is still classified as worth a per-chunk
    // fallback.  We only verify the worthwhile-flag here so the test
    // doesn't have to wait for the exponential-backoff sleeps; the actual
    // attempt count under retry is exercised by the existing
    // `test_retrying_http` covering the wrapper layer.
    {
        CountingHttp http;
        http.canned = {502, "boom"};
        EmbeddingClient client(make_cfg(), http);
        const auto result = client.embed_many_classified({"hello"});
        if (!result.retry_per_chunk_worthwhile)
            return fail("502 should still allow per-chunk fallback");
    }

    std::cout << "[test_embedding_client_no_retry_on_403] all assertions passed" << std::endl;
    return 0;
}
