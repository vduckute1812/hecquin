// Document + ingestion + drill-sample free functions.
//
// Part of the Option A store refactor: each LearningStore method for the
// `documents` / `vec_documents` / `ingested_files` tables forwards to one
// of these free functions so the SQL lives next to a pure `(sqlite3*,
// StatementCache&)` entry point.  This keeps the single-connection
// façade invariant intact (there is still one `LearningStore`) while
// giving tests a seam that does not require a full `LearningStore`.
#pragma once

#ifdef HECQUIN_WITH_SQLITE

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace hecquin::learning {
struct DocumentRecord;
namespace detail { class StatementCache; }
} // namespace hecquin::learning

namespace hecquin::learning::store::detail {

std::optional<int64_t>
upsert_document(sqlite3* db,
                learning::detail::StatementCache& /*cache — unused today*/,
                int embedding_dim,
                const DocumentRecord& doc,
                const std::vector<float>& embedding);

bool is_file_already_ingested(sqlite3* db,
                              const std::string& path,
                              const std::string& hash);

void record_ingested_file(sqlite3* db,
                          const std::string& path,
                          const std::string& hash);

/**
 * Drop every `documents` (and matching `vec_documents`) row whose `source`
 * equals `source`.  Returns the number of `documents` rows removed (0 when
 * the source was unknown).  Wrapped in a transaction so vec0 + documents
 * stay consistent on partial failure.  Used by the ingestor for atomic
 * per-file replace and for the optional --prune-missing pass.
 */
int purge_documents_for_source(sqlite3* db, const std::string& source);

/** Distinct `source` values currently present in `documents`. */
std::vector<std::string> list_document_sources(sqlite3* db);

/** Drop the row in `ingested_files` whose `path` equals `path`, if any. */
void delete_ingested_file(sqlite3* db, const std::string& path);

std::vector<std::string> sample_drill_sentences(sqlite3* db, int limit);

} // namespace hecquin::learning::store::detail

#endif // HECQUIN_WITH_SQLITE
