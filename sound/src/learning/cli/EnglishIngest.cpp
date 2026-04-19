#include "config/AppConfig.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/Ingestor.hpp"
#include "learning/LearningStore.hpp"

#include <charconv>
#include <iostream>
#include <string>

#ifndef DEFAULT_CONFIG_PATH
#define DEFAULT_CONFIG_PATH ConfigStore::kDefaultPath
#endif
#ifndef DEFAULT_PROMPTS_DIR
#define DEFAULT_PROMPTS_DIR nullptr
#endif

namespace {

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [options]\n"
        << "  --rebuild              Re-embed even files that are already ingested.\n"
        << "  --curriculum <dir>     Override curriculum root (default from config.env).\n"
        << "  --custom <dir>         Override custom corpus root.\n"
        << "  --chunk-chars N        Target chunk size in characters (default 1800).\n"
        << "  --batch-size N         Embeddings packed per HTTP request (default 16).\n"
        << "  -h, --help             Show this help.\n";
}

/**
 * Parse a small positive integer from `raw` into `out`.  Returns false if the
 * string is not a clean integer; `out` is unchanged on failure.
 */
bool parse_positive_int(const char* raw, int min_value, int& out) {
    if (!raw || !*raw) return false;
    const std::string_view sv{raw};
    int value = 0;
    const auto [end, ec] =
        std::from_chars(sv.data(), sv.data() + sv.size(), value);
    if (ec != std::errc{} || end != sv.data() + sv.size()) return false;
    if (value < min_value) return false;
    out = value;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    AppConfig app = AppConfig::load(DEFAULT_CONFIG_PATH, DEFAULT_PROMPTS_DIR);

    hecquin::learning::IngestorConfig icfg;
    icfg.curriculum_dir = app.learning.curriculum_dir;
    icfg.custom_dir = app.learning.custom_dir;

    bool rebuild = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--rebuild") {
            rebuild = true;
        } else if (arg == "--curriculum" && i + 1 < argc) {
            icfg.curriculum_dir = argv[++i];
        } else if (arg == "--custom" && i + 1 < argc) {
            icfg.custom_dir = argv[++i];
        } else if (arg == "--chunk-chars" && i + 1 < argc) {
            if (!parse_positive_int(argv[++i], 100, icfg.chunk_chars)) {
                std::cerr << "--chunk-chars requires a positive integer >= 100\n";
                return 2;
            }
        } else if (arg == "--batch-size" && i + 1 < argc) {
            if (!parse_positive_int(argv[++i], 1, icfg.embed_batch_size)) {
                std::cerr << "--batch-size requires a positive integer >= 1\n";
                return 2;
            }
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 2;
        }
    }
    icfg.force_rebuild = rebuild;

    hecquin::learning::LearningStore store(app.learning.db_path, app.ai.embedding_dim);
    if (!store.open()) {
        std::cerr << "Failed to open learning DB at " << app.learning.db_path << std::endl;
        return 1;
    }

    hecquin::learning::EmbeddingClient embedder(app.ai);
    if (!embedder.ready()) {
        std::cerr << "Embedding client is not configured "
                  << "(need GEMINI_API_KEY / OPENAI_API_KEY + libcurl)." << std::endl;
        return 1;
    }

    hecquin::learning::Ingestor ingestor(store, embedder, icfg);
    const auto report = ingestor.run();
    return report.chunks_failed > 0 ? 1 : 0;
}
