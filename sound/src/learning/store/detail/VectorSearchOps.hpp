// kNN search over the embedding column.
//
// The `LearningStore::query_top_k` façade method forwards into this free
// function so the vector-search code path can be exercised directly with
// a prepared `sqlite3*` + `StatementCache` (useful for tests that want
// to stage a corpus without spinning up the entire store).
#pragma once

#ifdef HECQUIN_WITH_SQLITE

#include <vector>

struct sqlite3;

namespace hecquin::learning {
struct RetrievedDocument;
namespace detail { class StatementCache; }
} // namespace hecquin::learning

namespace hecquin::learning::store::detail {

std::vector<RetrievedDocument>
query_top_k(sqlite3* db,
            learning::detail::StatementCache& cache,
            bool has_vec0,
            int embedding_dim,
            const std::vector<float>& embedding,
            int k);

} // namespace hecquin::learning::store::detail

#endif // HECQUIN_WITH_SQLITE
