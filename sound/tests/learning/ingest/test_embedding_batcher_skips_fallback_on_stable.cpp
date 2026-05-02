// Regression for issue #6 part 2: when a batch fails *stably* (e.g. 403),
// EmbeddingBatcher must NOT loop into per-chunk requests — that would
// just multiply the broken request by the slice size.

#include "ai/IHttpClient.hpp"
#include "config/ai/AiClientConfig.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/ingest/EmbeddingBatcher.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

using hecquin::ai::HttpResult;

int fail(const char* message) {
    std::cerr << "[test_embedding_batcher_skips_fallback_on_stable] FAIL: "
              << message << std::endl;
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
    using hecquin::learning::ingest::EmbeddingBatcher;

    // Stable failure: one HTTP call, N nullopts in the result.
    {
        CountingHttp http;
        http.canned = {403, R"({"error":"forbidden"})"};
        EmbeddingClient client(make_cfg(), http);

        EmbeddingBatcher batcher(client, /*batch_size=*/4);
        std::vector<std::string> slice{"a", "b", "c", "d"};
        const auto out = batcher.embed_slice(slice);

        if (out.size() != slice.size())
            return fail("batcher must return one entry per chunk");
        for (const auto& v : out) {
            if (v) return fail("stable failure must yield nullopt for every chunk");
        }
        if (http.call_count != 1) {
            std::cerr << "  observed " << http.call_count << " calls\n";
            return fail("stable failure must skip per-chunk fallback (one HTTP call)");
        }
    }

    // Success path: still works (sanity check).
    {
        CountingHttp http;
        http.canned = {200,
            R"({"data":[{"embedding":[1,2,3]},{"embedding":[4,5,6]}]})"};
        EmbeddingClient client(make_cfg(), http);

        EmbeddingBatcher batcher(client, /*batch_size=*/2);
        std::vector<std::string> slice{"a", "b"};
        const auto out = batcher.embed_slice(slice);

        if (out.size() != 2) return fail("success must return one entry per chunk");
        if (!out[0] || !out[1]) return fail("success entries must be populated");
        if (http.call_count != 1) return fail("success must use a single batched call");
    }

    std::cout << "[test_embedding_batcher_skips_fallback_on_stable] all assertions passed"
              << std::endl;
    return 0;
}
