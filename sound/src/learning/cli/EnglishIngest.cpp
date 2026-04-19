#include "config/AppConfig.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/Ingestor.hpp"
#include "learning/LearningStore.hpp"

#include <cstring>
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
    std::cerr << "Usage: " << prog << " [--rebuild] [--curriculum <dir>] [--custom <dir>] [--chunk-chars N]\n";
}

} // namespace

int main(int argc, char** argv) {
    hecquin::learning::IngestorConfig icfg;
    bool rebuild = false;

    AppConfig app = AppConfig::load(DEFAULT_CONFIG_PATH, DEFAULT_PROMPTS_DIR);
    icfg.curriculum_dir = app.learning.curriculum_dir;
    icfg.custom_dir = app.learning.custom_dir;

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
            icfg.chunk_chars = std::stoi(argv[++i]);
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
        std::cerr << "Embedding client is not configured (need GEMINI_API_KEY / OPENAI_API_KEY + libcurl).\n";
        return 1;
    }

    hecquin::learning::Ingestor ingestor(store, embedder, icfg);
    const auto report = ingestor.run();
    return report.chunks_failed > 0 ? 1 : 0;
}
