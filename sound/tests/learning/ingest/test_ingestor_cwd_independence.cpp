// Regression for issue #4: re-running the ingestor from a different cwd must
// hit the unchanged-skip branch on the second pass.  Pre-fix, the source
// identity was the literal joined path which differed per cwd, so the
// hash lookup missed and every file was re-embedded.

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
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

int fail(const char* message) {
    std::cerr << "[test_ingestor_cwd_independence] FAIL: " << message << std::endl;
    return 1;
}

fs::path make_tmp_dir(const char* tag) {
    std::string tpl = std::string("/tmp/hecquin_ingest_") + tag + "_XXXXXX";
    std::vector<char> buf(tpl.begin(), tpl.end());
    buf.push_back('\0');
    if (!mkdtemp(buf.data())) return {};
    return fs::path(buf.data());
}

void write_file(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p);
    out << body;
}

// Smart fake: parses the request body's `input` array length and returns
// that many fake-but-valid embeddings of the configured dimension.
class FakeEmbedHttp final : public hecquin::ai::IHttpClient {
public:
    int call_count = 0;
    int dim;

    explicit FakeEmbedHttp(int d) : dim(d) {}

    std::optional<HttpResult> post_json(const std::string& /*url*/,
                                        const std::string& /*bearer*/,
                                        const std::string& json_body,
                                        long /*timeout*/) override {
        ++call_count;
        // Cheap count of `input` entries: tally `","` separators inside
        // the input array.  Good enough — the EmbeddingClient builds the
        // body itself so its shape is stable.
        const std::string marker = "\"input\":[";
        const auto pos = json_body.find(marker);
        std::size_t count = 1;
        if (pos != std::string::npos) {
            const auto start = pos + marker.size();
            const auto end = json_body.find(']', start);
            if (end != std::string::npos) {
                count = 1;
                for (std::size_t i = start; i < end; ++i) {
                    if (json_body[i] == ',') ++count;
                }
                if (start == end) count = 0;
            }
        }
        std::ostringstream body;
        body << R"({"data":[)";
        for (std::size_t i = 0; i < count; ++i) {
            if (i) body << ',';
            body << R"({"embedding":[)";
            for (int d = 0; d < dim; ++d) {
                if (d) body << ',';
                body << "0.1";
            }
            body << "]}";
        }
        body << "]}";
        return HttpResult{200, body.str()};
    }
};

} // namespace

int main() {
    using namespace hecquin::learning;

    const fs::path corpus = make_tmp_dir("cwd_corpus");
    if (corpus.empty()) return fail("could not allocate corpus tmp dir");
    write_file(corpus / "lesson.txt",
               std::string(800, 'a'));  // single chunk under default budget
    write_file(corpus / "more.txt",
               std::string(800, 'b'));

    char db_buf[] = "/tmp/hecquin_ingest_cwd_db_XXXXXX";
    if (!mkdtemp(db_buf)) {
        fs::remove_all(corpus);
        return fail("could not allocate db tmp dir");
    }
    const std::string db_path = std::string(db_buf) + "/learning.sqlite";

    AiClientConfig cfg;
    cfg.api_key = "fake";
    cfg.embeddings_url = "https://unit.test/embeddings";
    cfg.embedding_model = "fake-model";
    cfg.embedding_dim = 8;

    FakeEmbedHttp http(cfg.embedding_dim);
    EmbeddingClient client(cfg, http);

    LearningStore store(db_path, cfg.embedding_dim);
    if (!store.open()) {
        fs::remove_all(corpus);
        fs::remove_all(db_buf);
        return fail("could not open store");
    }

    IngestorConfig icfg;
    icfg.curriculum_dir = corpus.string();
    icfg.custom_dir = "";

    // First run: from cwd=/tmp.
    fs::current_path("/tmp");
    Ingestor first(store, client, icfg);
    const auto r1 = first.run();
    if (r1.files_ingested != 2) {
        std::cerr << "  files_ingested=" << r1.files_ingested << "\n";
        fs::remove_all(corpus);
        fs::remove_all(db_buf);
        return fail("first run should ingest 2 files");
    }

    // Second run: from a *different* cwd.  Pre-fix the source string would
    // have differed and re-ingestion would happen.  Now: skipped == 2.
    fs::current_path(corpus);
    Ingestor second(store, client, icfg);
    const auto r2 = second.run();
    if (r2.files_skipped != 2 || r2.files_ingested != 0) {
        std::cerr << "  skipped=" << r2.files_skipped
                  << " ingested=" << r2.files_ingested << "\n";
        fs::current_path("/tmp");
        fs::remove_all(corpus);
        fs::remove_all(db_buf);
        return fail("second run should skip both files unchanged");
    }

    fs::current_path("/tmp");
    fs::remove_all(corpus);
    fs::remove_all(db_buf);
    std::cout << "[test_ingestor_cwd_independence] all assertions passed" << std::endl;
    return 0;
}
