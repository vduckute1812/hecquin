// Regression for issue #7: --prune-missing must drop documents whose source
// file disappeared from disk; without the flag, those rows must remain so
// re-runs on partial corpora don't accidentally garbage-collect data.

#include "ai/IHttpClient.hpp"
#include "config/ai/AiClientConfig.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/Ingestor.hpp"
#include "learning/store/LearningStore.hpp"

#include <algorithm>
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
    std::cerr << "[test_ingestor_prune_missing] FAIL: " << message << std::endl;
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

bool has_substr(const std::vector<std::string>& v, const std::string& needle) {
    return std::any_of(v.begin(), v.end(), [&](const std::string& s) {
        return s.find(needle) != std::string::npos;
    });
}

} // namespace

int main() {
    using namespace hecquin::learning;

    char tmp_buf[] = "/tmp/hecquin_ingest_prune_XXXXXX";
    if (!mkdtemp(tmp_buf)) return fail("could not allocate tmp dir");
    const fs::path root = tmp_buf;
    const fs::path corpus = root / "corpus";
    fs::create_directories(corpus);
    const std::string db_path = (root / "db.sqlite").string();

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

    write_file(corpus / "alpha.txt", "alpha contents are short.\n");
    write_file(corpus / "beta.txt",  "beta contents go here too.\n");

    IngestorConfig icfg;
    icfg.curriculum_dir = corpus.string();
    icfg.custom_dir = "";

    {
        Ingestor first(store, client, icfg);
        const auto r = first.run();
        if (r.files_ingested != 2) {
            fs::remove_all(root);
            return fail("first run must ingest both files");
        }
    }

    // Delete beta.txt from disk.
    fs::remove(corpus / "beta.txt");

    // Second run, no --prune-missing: beta still in DB.
    {
        Ingestor second(store, client, icfg);
        const auto r = second.run();
        if (r.files_pruned != 0) {
            fs::remove_all(root);
            return fail("default run must not prune");
        }
        const auto sources = store.list_document_sources();
        if (!has_substr(sources, "beta.txt")) {
            fs::remove_all(root);
            return fail("without --prune-missing, beta.txt rows must persist");
        }
    }

    // Third run, with --prune-missing: beta gets dropped, alpha kept.
    icfg.prune_missing_sources = true;
    {
        Ingestor third(store, client, icfg);
        const auto r = third.run();
        if (r.files_pruned != 1) {
            std::cerr << "  files_pruned=" << r.files_pruned << "\n";
            fs::remove_all(root);
            return fail("--prune-missing should drop exactly one file");
        }
        const auto sources = store.list_document_sources();
        if (has_substr(sources, "beta.txt")) {
            fs::remove_all(root);
            return fail("beta.txt rows must be removed");
        }
        if (!has_substr(sources, "alpha.txt")) {
            fs::remove_all(root);
            return fail("alpha.txt rows must remain");
        }
    }

    fs::remove_all(root);
    std::cout << "[test_ingestor_prune_missing] all assertions passed" << std::endl;
    return 0;
}
