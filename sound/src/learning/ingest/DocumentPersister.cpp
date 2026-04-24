#include "learning/ingest/DocumentPersister.hpp"

#include "learning/store/LearningStore.hpp"

namespace hecquin::learning::ingest {

DocumentPersister::DocumentPersister(LearningStore& store) : store_(store) {}

bool DocumentPersister::persist(const FileParams& params,
                                std::size_t idx,
                                const std::string& body,
                                const std::vector<float>& embedding) {
    DocumentRecord rec;
    rec.source = params.source;
    rec.kind = params.kind;
    rec.title = params.title_prefix + "#" + std::to_string(idx + 1);
    rec.body = body;
    rec.metadata_json = "{\"chunk_index\":" + std::to_string(idx) +
                        ",\"chunks_total\":" + std::to_string(params.total_chunks) + "}";
    return store_.upsert_document(rec, embedding).has_value();
}

} // namespace hecquin::learning::ingest
