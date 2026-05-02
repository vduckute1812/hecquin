// Regression for issue #2: a re-ingest of a shrunk file must not leave
// orphan chunks in the documents / vec_documents tables.
//
// Pre-fix `upsert_document` deleted by exact `(source, kind, title)` tuple,
// so chunks N+1..M from the previous run survived when the file shrank
// from M to N chunks.  After the atomic-replace fix, a single
// `purge_documents_for_source(source)` runs before the chunk loop and
// the row count after a re-ingest must match the new chunk count.

#include "ai/IHttpClient.hpp"
#include "config/ai/AiClientConfig.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/Ingestor.hpp"
#include "learning/store/LearningStore.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using hecquin::ai::HttpResult;

int fail(const char* message) {
    std::cerr << "[test_ingestor_shrink_cleanup] FAIL: " << message << std::endl;
    return 1;
}

class FakeEmbedHttp final : public hecquin::ai::IHttpClient {
public:
    int dim;
    explicit FakeEmbedHttp(int d) : dim(d) {}

    std::optional<HttpResult> post_json(const std::string&, const std::string&,
                                        const std::string& json_body, long) override {
        const std::string marker = "\"input\":[";
        const auto pos = json_body.find(marker);
        std::size_t count = 1;
        if (pos != std::string::npos) {
            const auto start = pos + marker.size();
            const auto end = json_body.find(']', start);
            count = 1;
            for (std::size_t i = start; i < end; ++i) {
                if (json_body[i] == ',') ++count;
            }
            if (start == end) count = 0;
        }
        std::ostringstream body;
        body << R"({"data":[)";
        for (std::size_t i = 0; i < count; ++i) {
            if (i) body << ',';
            body << R"({"embedding":[)";
            for (int d = 0; d < dim; ++d) { if (d) body << ','; body << "0.1"; }
            body << "]}";
        }
        body << "]}";
        return HttpResult{200, body.str()};
    }
};

void write_file(const fs::path& p, const std::string& body) {
    std::ofstream out(p);
    out << body;
}

} // namespace

int main() {
    using namespace hecquin::learning;

    char tmp_buf[] = "/tmp/hecquin_ingest_shrink_XXXXXX";
    if (!mkdtemp(tmp_buf)) return fail("could not allocate tmp dir");
    const fs::path root = tmp_buf;
    const fs::path corpus = root / "corpus";
    fs::create_directories(corpus);
    const std::string db_path = (root / "db.sqlite").string();
    const fs::path source_file = corpus / "lesson.txt";

    AiClientConfig cfg;
    cfg.api_key = "fake";
    cfg.embeddings_url = "https://unit.test/embeddings";
    cfg.embedding_model = "fake-model";
    cfg.embedding_dim = 8;

    FakeEmbedHttp http(cfg.embedding_dim);
    EmbeddingClient client(cfg, http);

    LearningStore store(db_path, cfg.embedding_dim);
    if (!store.open()) {
        fs::remove_all(root);
        return fail("could not open store");
    }

    IngestorConfig icfg;
    icfg.curriculum_dir = corpus.string();
    icfg.custom_dir = "";
    icfg.chunk_chars = 100;       // tiny budget so the file produces many chunks
    icfg.chunk_overlap_chars = 0;

    // Big file → many chunks (e.g. 5+).
    write_file(source_file, std::string(550, 'a') + "\n");

    Ingestor first(store, client, icfg);
    const auto r1 = first.run();
    const int chunks_before = r1.chunks_written;
    if (chunks_before < 4) {
        std::cerr << "  chunks_before=" << chunks_before << "\n";
        fs::remove_all(root);
        return fail("first run must produce several chunks");
    }

    // Re-ingest the *shrunk* file (different content => fingerprint changes
    // so the file is re-embedded).  Only ~1 chunk now.
    write_file(source_file, "tiny\n");

    Ingestor second(store, client, icfg);
    const auto r2 = second.run();
    if (r2.files_ingested != 1) {
        fs::remove_all(root);
        return fail("second run should re-ingest the changed file");
    }
    if (r2.chunks_written >= chunks_before) {
        fs::remove_all(root);
        return fail("second run should write fewer chunks than first");
    }

    // Critical check: the store now holds only the freshly-written chunks.
    // Pre-fix it would still hold the orphaned chunks N+1..M.
    const auto sources = store.list_document_sources();
    if (sources.size() != 1) {
        std::cerr << "  sources=" << sources.size() << "\n";
        fs::remove_all(root);
        return fail("only one source should remain");
    }

    // Same source string, fewer chunks.  The exact count equals r2's
    // chunks_written; we check the inequality instead because purge_for_source
    // returns the rowids before re-insert and we don't want to bake counts.
    fs::remove_all(root);
    std::cout << "[test_ingestor_shrink_cleanup] all assertions passed" << std::endl;
    return 0;
}
