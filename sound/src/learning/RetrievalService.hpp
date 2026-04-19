#pragma once

#include "learning/LearningStore.hpp"

#include <string>
#include <vector>

namespace hecquin::learning {

class EmbeddingClient;

class RetrievalService {
public:
    RetrievalService(LearningStore& store, EmbeddingClient& embed);

    /** Embed `query` then return at most `k` nearest documents (by cosine distance). */
    std::vector<RetrievedDocument> top_k(const std::string& query, int k) const;

    /** Convenience: concatenate `top_k` bodies into a single context string capped at `max_chars`. */
    std::string build_context(const std::string& query, int k, int max_chars = 4000) const;

private:
    LearningStore& store_;
    EmbeddingClient& embed_;
};

} // namespace hecquin::learning
