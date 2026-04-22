// kNN search over the embedding column.
//
// Two code paths:
//   * `vec0` virtual table via sqlite-vec (fast, SQL-native MATCH ... k)
//   * BLOB fallback — a full scan with cosine distance, used when the build
//     lacks sqlite-vec. Keeps the pipeline working at small corpus sizes.

#include "learning/LearningStore.hpp"

#ifdef HECQUIN_WITH_SQLITE
#include "learning/internal/SqliteHelpers.hpp"
#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <queue>
#include <utility>
#endif

namespace hecquin::learning {

#ifdef HECQUIN_WITH_SQLITE
using detail::prepare_or_log;
#endif

std::vector<RetrievedDocument>
LearningStore::query_top_k(const std::vector<float>& embedding, int k) const {
    std::vector<RetrievedDocument> out;
#ifndef HECQUIN_WITH_SQLITE
    (void)embedding; (void)k;
    return out;
#else
    if (!db_ || k <= 0) return out;
    if (static_cast<int>(embedding.size()) != embedding_dim_) {
        std::cerr << "[LearningStore] query dim mismatch: got " << embedding.size()
                  << " expected " << embedding_dim_ << std::endl;
        return out;
    }

    if (has_vec0_) {
        auto q = prepare_or_log(db_,
            "SELECT d.id, d.source, d.kind, d.title, d.body, d.metadata_json, v.distance "
            "FROM vec_documents v JOIN documents d ON d.id = v.rowid "
            "WHERE v.embedding MATCH ? AND k = ? ORDER BY v.distance;",
            "topk.vec");
        if (!q) return out;
        sqlite3_bind_blob(q.get(), 1, embedding.data(),
                          static_cast<int>(embedding.size() * sizeof(float)),
                          SQLITE_TRANSIENT);
        sqlite3_bind_int(q.get(), 2, k);
        while (sqlite3_step(q.get()) == SQLITE_ROW) {
            RetrievedDocument r;
            r.doc.id = sqlite3_column_int64(q.get(), 0);
            auto str = [&](int col) {
                const unsigned char* t = sqlite3_column_text(q.get(), col);
                return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
            };
            r.doc.source = str(1);
            r.doc.kind = str(2);
            r.doc.title = str(3);
            r.doc.body = str(4);
            r.doc.metadata_json = str(5);
            r.distance = static_cast<float>(sqlite3_column_double(q.get(), 6));
            out.push_back(std::move(r));
        }
        return out;
    }

    auto scan = prepare_or_log(db_,
        "SELECT d.id, d.source, d.kind, d.title, d.body, d.metadata_json, v.embedding "
        "FROM vec_documents v JOIN documents d ON d.id = v.rowid;",
        "topk.scan");
    if (!scan) return out;

    auto norm = [](const float* v, int n) {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += static_cast<double>(v[i]) * v[i];
        return std::sqrt(s);
    };
    const double q_norm = norm(embedding.data(), static_cast<int>(embedding.size()));
    if (q_norm < 1e-9) return out;

    using Scored = std::pair<float, RetrievedDocument>;
    auto cmp = [](const Scored& a, const Scored& b) { return a.first < b.first; };
    std::priority_queue<Scored, std::vector<Scored>, decltype(cmp)> heap(cmp);

    while (sqlite3_step(scan.get()) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(scan.get(), 6);
        const int bytes = sqlite3_column_bytes(scan.get(), 6);
        const int n = bytes / static_cast<int>(sizeof(float));
        if (n != static_cast<int>(embedding.size())) continue;
        const float* vec = static_cast<const float*>(blob);
        double dot = 0.0;
        for (int i = 0; i < n; ++i) dot += static_cast<double>(embedding[i]) * vec[i];
        const double vn = norm(vec, n);
        if (vn < 1e-9) continue;
        const float distance = static_cast<float>(1.0 - dot / (q_norm * vn));

        RetrievedDocument r;
        r.doc.id = sqlite3_column_int64(scan.get(), 0);
        auto str = [&](int col) {
            const unsigned char* t = sqlite3_column_text(scan.get(), col);
            return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
        };
        r.doc.source = str(1);
        r.doc.kind = str(2);
        r.doc.title = str(3);
        r.doc.body = str(4);
        r.doc.metadata_json = str(5);
        r.distance = distance;

        if (static_cast<int>(heap.size()) < k) {
            heap.emplace(distance, std::move(r));
        } else if (distance < heap.top().first) {
            heap.pop();
            heap.emplace(distance, std::move(r));
        }
    }

    while (!heap.empty()) {
        out.push_back(heap.top().second);
        heap.pop();
    }
    std::reverse(out.begin(), out.end());
    return out;
#endif
}

} // namespace hecquin::learning
