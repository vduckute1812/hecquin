#include "learning/RetrievalService.hpp"

#include "common/EnvParse.hpp"
#include "learning/EmbeddingClient.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace hecquin::learning {

RetrievalService::RetrievalService(LearningStore& store, EmbeddingClient& embed)
    : RetrievalService(store, embed, resolve_default_capacity_()) {}

RetrievalService::RetrievalService(LearningStore& store,
                                   EmbeddingClient& embed,
                                   std::size_t cache_size)
    : store_(store), embed_(embed), cache_capacity_(cache_size) {
    lru_map_.reserve(cache_capacity_);
}

std::vector<RetrievedDocument> RetrievalService::top_k(const std::string& query, int k) const {
    if (!store_.is_open() || k <= 0) return {};

    const std::string key = normalise_query(query);
    if (cache_capacity_ > 0) {
        if (const std::vector<float>* cached = cache_lookup_(key)) {
            return store_.query_top_k(*cached, k);
        }
    }

    auto emb = embed_.embed(query);
    if (!emb) return {};

    if (cache_capacity_ > 0 && !key.empty() && !emb->empty()) {
        cache_put_(key, *emb);
    }
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

void RetrievalService::clear_cache() {
    lru_keys_.clear();
    lru_map_.clear();
}

std::vector<std::string> RetrievalService::cache_keys_for_test() const {
    return std::vector<std::string>(lru_keys_.begin(), lru_keys_.end());
}

std::string RetrievalService::normalise_query(const std::string& query) {
    std::string out;
    out.reserve(query.size());
    bool in_space = true;  // collapse leading whitespace by default
    for (unsigned char ch : query) {
        if (std::isspace(ch)) {
            if (!in_space) {
                out.push_back(' ');
                in_space = true;
            }
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(ch)));
        in_space = false;
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

const std::vector<float>* RetrievalService::cache_lookup_(const std::string& key) const {
    auto it = lru_map_.find(key);
    if (it == lru_map_.end()) return nullptr;
    // Bump to MRU.
    lru_keys_.splice(lru_keys_.begin(), lru_keys_, it->second.second);
    it->second.second = lru_keys_.begin();
    return &it->second.first;
}

void RetrievalService::cache_put_(const std::string& key, std::vector<float> embedding) const {
    auto it = lru_map_.find(key);
    if (it != lru_map_.end()) {
        it->second.first = std::move(embedding);
        lru_keys_.splice(lru_keys_.begin(), lru_keys_, it->second.second);
        it->second.second = lru_keys_.begin();
        return;
    }

    while (lru_keys_.size() >= cache_capacity_ && !lru_keys_.empty()) {
        const std::string victim = std::move(lru_keys_.back());
        lru_keys_.pop_back();
        lru_map_.erase(victim);
    }

    lru_keys_.push_front(key);
    lru_map_.emplace(key, std::make_pair(std::move(embedding), lru_keys_.begin()));
}

std::size_t RetrievalService::resolve_default_capacity_() {
    std::size_t cap = kDefaultCacheSize;
    hecquin::common::env::parse_size("HECQUIN_RAG_CACHE_SIZE", cap);
    return cap;
}

} // namespace hecquin::learning
