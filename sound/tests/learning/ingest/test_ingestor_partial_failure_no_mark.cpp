// Regression for issue #3: when chunks fail mid-file, the file must NOT be
// recorded in `ingested_files` and the partial chunks must be rolled back.
// Pre-fix the file was marked ingested as long as `ok > 0`, so a future
// run hit the unchanged-skip branch and never retried the failed chunks.

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

namespace fs = std::filesystem;

namespace {

int fail(const char* message) {
    std::cerr << "[test_ingestor_partial_failure_no_mark] FAIL: " << message << std::endl;
    return 1;
}

// Returns 200 for the first `n_ok_calls` invocations, then 200 for the
// rest unless the request matches a "poison" chunk, in which case it
// flips to 500 to trigger a per-chunk failure.  We simulate "chunk #3
// fails" by counting batched calls (we drive the test with batch_size=1
// so one HTTP call == one chunk).
class IndexedFakeHttp final : public hecquin::ai::IHttpClient {
public:
    int dim;
    int call_count = 0;
    int fail_at = -1;        // 1-indexed call number that should return 500

    explicit IndexedFakeHttp(int d) : dim(d) {}

    std::optional<HttpResult> post_json(const std::string&, const std::string&,
                                        const std::string& json_body, long) override {
        ++call_count;
        // 403 is classified as a stable failure: EmbeddingClient won't
        // retry it, EmbeddingBatcher won't fall back per-chunk, and the
        // chunk_failed counter ticks up exactly once per targeted call.
        if (call_count == fail_at) {
            return HttpResult{403, R"({"error":"injected"})"};
        }
        // Count `input` entries
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

    char tmp_buf[] = "/tmp/hecquin_ingest_partial_XXXXXX";
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

    IndexedFakeHttp http(cfg.embedding_dim);
    EmbeddingClient client(cfg, http);

    LearningStore store(db_path, cfg.embedding_dim);
    if (!store.open()) {
        fs::remove_all(root);
        return fail("could not open store");
    }

    IngestorConfig icfg;
    icfg.curriculum_dir = corpus.string();
    icfg.custom_dir = "";
    icfg.chunk_chars = 100;
    icfg.chunk_overlap_chars = 0;
    icfg.embed_batch_size = 1;       // one call per chunk → easy to target
    // Disable circuit breaker so the run actually completes the file.
    icfg.max_consecutive_chunk_failures = 0;

    write_file(source_file, std::string(550, 'a') + "\n");

    // After a probe call (#1 inside Ingestor::run), the next 5 calls are
    // chunk #1..#5 of the file.  Fail call #4 → chunk #3 fails, others OK.
    // The probe call is #1; chunks start at call #2.
    http.fail_at = 1 + 3;

    Ingestor ingestor(store, client, icfg);
    const auto report = ingestor.run();

    if (report.files_ingested != 0) {
        std::cerr << "  files_ingested=" << report.files_ingested << "\n";
        fs::remove_all(root);
        return fail("partial failure must NOT mark file as ingested");
    }
    if (report.chunks_failed < 1) {
        fs::remove_all(root);
        return fail("at least one chunk should be reported failed");
    }

    // Rollback should have wiped any partially-written chunks for the source.
    const auto sources = store.list_document_sources();
    if (!sources.empty()) {
        std::cerr << "  sources=" << sources.size() << "\n";
        fs::remove_all(root);
        return fail("partial failure must roll back to zero source chunks");
    }

    // A second run with no injected failure must re-ingest the file.
    http.fail_at = -1;
    http.call_count = 0;

    Ingestor retry(store, client, icfg);
    const auto r2 = retry.run();
    if (r2.files_ingested != 1) {
        std::cerr << "  files_ingested(retry)=" << r2.files_ingested << "\n";
        fs::remove_all(root);
        return fail("retry run should re-ingest the file (not skip it)");
    }

    fs::remove_all(root);
    std::cout << "[test_ingestor_partial_failure_no_mark] all assertions passed" << std::endl;
    return 0;
}
