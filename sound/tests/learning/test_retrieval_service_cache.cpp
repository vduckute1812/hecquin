// RetrievalService LRU cache: pins the normalisation rule, the recency
// bumping, the eviction policy, and the end-to-end "embed call count is
// 1 for repeated queries" property.  Uses a counting fake IHttpClient
// behind a real EmbeddingClient + LearningStore, so the cache contract
// is exercised through `top_k`.

#include "ai/IHttpClient.hpp"
#include "config/ai/AiClientConfig.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/RetrievalService.hpp"
#include "learning/store/LearningStore.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using hecquin::ai::HttpResult;
using hecquin::learning::EmbeddingClient;
using hecquin::learning::LearningStore;
using hecquin::learning::RetrievalService;

int failures = 0;

void expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "[FAIL] " << label << std::endl;
        ++failures;
    }
}

class CountingHttp final : public hecquin::ai::IHttpClient {
public:
    HttpResult canned{200, R"({"data":[{"embedding":[1.0,0.0,0.0,0.0]}]})"};
    int calls = 0;

    std::optional<HttpResult> post_json(const std::string& /*url*/,
                                        const std::string& /*bearer*/,
                                        const std::string& /*json_body*/,
                                        long /*timeout*/) override {
        ++calls;
        return canned;
    }
};

std::string make_tmp_db() {
    char buf[] = "/tmp/hecquin_retrieval_XXXXXX";
    const int fd = mkstemp(buf);
    if (fd < 0) return {};
    close(fd);
    std::remove(buf);
    return std::string(buf) + ".sqlite";
}

} // namespace

int main() {
    // 1. Normalisation: lowercase + collapse whitespace + trim.
    {
        expect(RetrievalService::normalise_query("Hello   World") == "hello world",
               "lowercases + collapses internal whitespace");
        expect(RetrievalService::normalise_query("   spaced   out  ") == "spaced out",
               "trims leading + trailing whitespace");
        expect(RetrievalService::normalise_query("") == "",
               "empty input stays empty");
        expect(RetrievalService::normalise_query("\t\nMixed\nWHITESPACE\t") == "mixed whitespace",
               "treats tabs/newlines as whitespace");
    }

    constexpr int kDim = 4;
    const std::string db_path = make_tmp_db();
    if (db_path.empty()) {
        std::cerr << "[test_retrieval_service_cache] cannot create tmp db\n";
        return 1;
    }

    LearningStore store(db_path, kDim);
    if (!store.open()) {
        std::remove(db_path.c_str());
        std::cerr << "[test_retrieval_service_cache] store.open() failed\n";
        return 1;
    }

    // Seed one document so `query_top_k` returns something.  The
    // canned embedding from CountingHttp matches this vector, so cosine
    // ranking is deterministic.
    {
        hecquin::learning::DocumentRecord d;
        d.source = "test";
        d.kind = "custom";
        d.title = "Alpha";
        d.body = "alpha body";
        const std::vector<float> v{1.0f, 0.0f, 0.0f, 0.0f};
        if (!store.upsert_document(d, v)) {
            std::remove(db_path.c_str());
            std::cerr << "[test_retrieval_service_cache] upsert failed\n";
            return 1;
        }
    }

    AiClientConfig embed_cfg;
    embed_cfg.api_key = "fake";
    embed_cfg.embeddings_url = "https://unit.test/embeddings";
    embed_cfg.embedding_model = "fake-embed";
    embed_cfg.embedding_dim = kDim;

    // 2. Cache hit on identical-after-normalisation query: only one
    //    embed HTTP call should be made.
    {
        CountingHttp http;
        EmbeddingClient embed(embed_cfg, http);
        RetrievalService rs(store, embed, /*cache_size=*/4);

        const auto a = rs.top_k("hello world", 1);
        expect(!a.empty(), "first lookup returns at least one doc");
        const int after_first = http.calls;

        const auto b = rs.top_k("HELLO   World", 1);
        expect(!b.empty(), "second lookup returns at least one doc");

        expect(http.calls == after_first,
               "cache hit short-circuits the embed HTTP call");
        expect(rs.cache_size_for_test() == 1,
               "single distinct query gives one cache entry");
    }

    // 3. Eviction order: capacity = 2, three distinct queries → only
    //    the two most recent survive, ordered MRU → LRU.
    {
        CountingHttp http;
        EmbeddingClient embed(embed_cfg, http);
        RetrievalService rs(store, embed, /*cache_size=*/2);

        rs.top_k("alpha", 1);
        rs.top_k("beta", 1);
        rs.top_k("gamma", 1);

        const auto keys = rs.cache_keys_for_test();
        expect(rs.cache_size_for_test() == 2, "cache capped at 2");
        expect(!keys.empty() && keys.front() == "gamma",
               "most recent insert sits at MRU slot");
        expect(keys.size() == 2 && keys.back() == "beta",
               "alpha was evicted, beta is now LRU");
    }

    // 4. Recency bump: re-querying an existing key should move it to
    //    MRU and protect it from the next eviction.
    {
        CountingHttp http;
        EmbeddingClient embed(embed_cfg, http);
        RetrievalService rs(store, embed, /*cache_size=*/2);

        rs.top_k("alpha", 1);
        rs.top_k("beta", 1);
        rs.top_k("alpha", 1);   // bump alpha to MRU
        rs.top_k("gamma", 1);   // beta should evict, alpha must survive

        const auto keys = rs.cache_keys_for_test();
        expect(rs.cache_size_for_test() == 2, "cache stays capped after bump");
        expect(!keys.empty() && keys.front() == "gamma",
               "newest insert is MRU after bump");
        expect(keys.size() == 2 && keys.back() == "alpha",
               "bumped key survives eviction; beta evicted");
    }

    // 5. Disabled cache (size=0) — every call hits the network.
    {
        CountingHttp http;
        EmbeddingClient embed(embed_cfg, http);
        RetrievalService rs(store, embed, /*cache_size=*/0);

        rs.top_k("repeat", 1);
        rs.top_k("repeat", 1);
        rs.top_k("repeat", 1);

        expect(http.calls == 3, "disabled cache forwards every embed call");
        expect(rs.cache_size_for_test() == 0, "no entries cached when size=0");
    }

    // 6. clear_cache resets state.
    {
        CountingHttp http;
        EmbeddingClient embed(embed_cfg, http);
        RetrievalService rs(store, embed, /*cache_size=*/4);

        rs.top_k("hello", 1);
        rs.top_k("world", 1);
        expect(rs.cache_size_for_test() == 2, "two entries before clear");
        rs.clear_cache();
        expect(rs.cache_size_for_test() == 0, "cache empty after clear_cache");
    }

    std::remove(db_path.c_str());

    if (failures == 0) {
        std::cout << "[test_retrieval_service_cache] OK" << std::endl;
        return 0;
    }
    std::cerr << "[test_retrieval_service_cache] " << failures
              << " failure(s)" << std::endl;
    return 1;
}
