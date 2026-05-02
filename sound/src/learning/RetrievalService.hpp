#pragma once

#include "learning/store/LearningStore.hpp"

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace hecquin::learning {

class EmbeddingClient;

/**
 * RAG retrieval facade.  Embeds a query, then asks `LearningStore` for
 * the nearest documents.  A small bounded LRU cache (keyed by the
 * normalised query text) sits in front of `EmbeddingClient::embed` so
 * back-to-back tutor turns that re-ask the same question skip the
 * embedding round-trip.
 *
 * The cache is intentionally tiny — typical sessions reuse only the
 * last few queries.  Override the size with `HECQUIN_RAG_CACHE_SIZE`
 * (0 disables the cache).  Threading: `RetrievalService` is expected
 * to be driven from a single thread (the listener / tutor turn loop);
 * the cache is *not* internally synchronised.
 */
class RetrievalService {
public:
    /** Built-in default cache size (entries). */
    static constexpr std::size_t kDefaultCacheSize = 16;

    RetrievalService(LearningStore& store, EmbeddingClient& embed);
    RetrievalService(LearningStore& store,
                     EmbeddingClient& embed,
                     std::size_t cache_size);

    /** Embed `query` (cache aware) then return at most `k` nearest documents. */
    std::vector<RetrievedDocument> top_k(const std::string& query, int k) const;

    /** Convenience: concatenate `top_k` bodies into a single context string capped at `max_chars`. */
    std::string build_context(const std::string& query, int k, int max_chars = 4000) const;

    /** Flush the embedding cache.  Useful between tests / lessons. */
    void clear_cache();

    /** Current number of cached entries. */
    std::size_t cache_size_for_test() const { return lru_keys_.size(); }

    /** Configured cache capacity (0 = disabled). */
    std::size_t cache_capacity_for_test() const { return cache_capacity_; }

    /** Iteration order: MRU → LRU. */
    std::vector<std::string> cache_keys_for_test() const;

    /**
     * Normalised cache key for `query`.  Exposed for tests so the
     * normalisation rule can be pinned without going through
     * `top_k`.
     */
    static std::string normalise_query(const std::string& query);

private:
    /** Look up an embedding by normalised key; bumps recency on hit. */
    const std::vector<float>* cache_lookup_(const std::string& key) const;

    /** Insert / replace a cache entry, evicting LRU entries when over capacity. */
    void cache_put_(const std::string& key, std::vector<float> embedding) const;

    static std::size_t resolve_default_capacity_();

    LearningStore&    store_;
    EmbeddingClient&  embed_;
    std::size_t       cache_capacity_;

    // Cache state is mutated from `top_k` (which is logically const), so
    // keep the storage `mutable`.  Single-threaded contract — see class
    // doc.
    mutable std::list<std::string> lru_keys_;
    mutable std::unordered_map<std::string,
        std::pair<std::vector<float>, std::list<std::string>::iterator>> lru_map_;
};

} // namespace hecquin::learning
