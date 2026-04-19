#include "learning/RetrievalService.hpp"

#include "learning/EmbeddingClient.hpp"

#include <sstream>

namespace hecquin::learning {

RetrievalService::RetrievalService(LearningStore& store, EmbeddingClient& embed)
    : store_(store), embed_(embed) {}

std::vector<RetrievedDocument> RetrievalService::top_k(const std::string& query, int k) const {
    if (!store_.is_open() || k <= 0) return {};
    auto emb = embed_.embed(query);
    if (!emb) return {};
    return store_.query_top_k(*emb, k);
}

std::string RetrievalService::build_context(const std::string& query, int k, int max_chars) const {
    const auto hits = top_k(query, k);
    if (hits.empty()) return {};
    std::ostringstream oss;
    int written = 0;
    for (size_t i = 0; i < hits.size(); ++i) {
        const auto& h = hits[i];
        std::ostringstream snip;
        snip << "[#" << (i + 1) << " kind=" << h.doc.kind << " title=" << h.doc.title << "]\n"
             << h.doc.body << "\n";
        const std::string chunk = snip.str();
        if (max_chars > 0 && written + static_cast<int>(chunk.size()) > max_chars) {
            const int remain = max_chars - written;
            if (remain > 20) oss << chunk.substr(0, static_cast<size_t>(remain));
            break;
        }
        oss << chunk;
        written += static_cast<int>(chunk.size());
    }
    return oss.str();
}

} // namespace hecquin::learning
