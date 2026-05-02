#include "ai/IHttpClient.hpp"
#include "ai/LoggingHttpClient.hpp"
#include "cli/DefaultPaths.hpp"
#include "common/PathUtils.hpp"
#include "config/AppConfig.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/Ingestor.hpp"
#include "learning/cli/CsvApiCallSink.hpp"
#include "learning/cli/RunSummaryCsv.hpp"
#include "learning/cli/StoreApiSink.hpp"
#include "learning/store/LearningStore.hpp"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [options]\n"
        << "  --rebuild              Re-embed even files that are already ingested.\n"
        << "  --curriculum <dir>     Override curriculum root (default from config.env).\n"
        << "  --custom <dir>         Override custom corpus root.\n"
        << "  --chunk-chars N        Target chunk size in characters (default 1800).\n"
        << "  --batch-size N         Embeddings packed per HTTP request (default 16; 1 disables array batching).\n"
        << "  --prune-missing        After ingest, drop documents whose source file is no longer on disk.\n"
        << "  --max-fail-streak N    Abort after N consecutive failed chunks (default 50; 0 disables).\n"
        << "  --deadline N           Wall-clock deadline for the whole run, in seconds (default 0 = unbounded).\n"
        << "  --max-file-bytes N     Skip source files larger than N bytes (default 52428800 = 50 MiB; 0 disables).\n"
        << "  --api-log-csv <path>   Append per-/embeddings-call rows here (env: HECQUIN_INGEST_API_LOG_CSV).\n"
        << "  --run-summary-csv <path>  Append one row per ingest run here (env: HECQUIN_INGEST_RUN_SUMMARY_CSV).\n"
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

/** Same as `parse_positive_int` but for `long long` (file-size flags). */
bool parse_positive_ll(const char* raw, long long min_value, long long& out) {
    if (!raw || !*raw) return false;
    const std::string_view sv{raw};
    long long value = 0;
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

    struct IntFlag { std::string_view name; int min; int* dest; std::string_view error; };
    const IntFlag int_flags[] = {
        {"--chunk-chars",     100, &icfg.chunk_chars,
         "--chunk-chars requires a positive integer >= 100"},
        {"--batch-size",        1, &icfg.embed_batch_size,
         "--batch-size requires a positive integer >= 1"},
        {"--max-fail-streak",   0, &icfg.max_consecutive_chunk_failures,
         "--max-fail-streak requires a non-negative integer"},
        {"--deadline",          0, &icfg.run_deadline_seconds,
         "--deadline requires a non-negative integer (seconds)"},
    };

    bool rebuild = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--rebuild") {
            rebuild = true;
        } else if (arg == "--prune-missing") {
            icfg.prune_missing_sources = true;
        } else if (arg == "--curriculum" && i + 1 < argc) {
            icfg.curriculum_dir = argv[++i];
        } else if (arg == "--custom" && i + 1 < argc) {
            icfg.custom_dir = argv[++i];
        } else if (arg == "--max-file-bytes" && i + 1 < argc) {
            if (!parse_positive_ll(argv[++i], 0, icfg.max_file_bytes)) {
                std::cerr << "--max-file-bytes requires a non-negative integer (bytes)\n";
                return 2;
            }
        } else if (arg == "--api-log-csv" && i + 1 < argc) {
            app.learning.ingest_api_log_csv = argv[++i];
        } else if (arg == "--run-summary-csv" && i + 1 < argc) {
            app.learning.ingest_run_summary_csv = argv[++i];
        } else {
            const IntFlag* matched = nullptr;
            for (const auto& f : int_flags) {
                if (arg == f.name) { matched = &f; break; }
            }
            if (matched && i + 1 < argc) {
                if (!parse_positive_int(argv[++i], matched->min, *matched->dest)) {
                    std::cerr << matched->error << '\n';
                    return 2;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << std::endl;
                print_usage(argv[0]);
                return 2;
            }
        }
    }
    icfg.force_rebuild = rebuild;

    // Anchor any CLI-supplied CSV paths against the config dir so they
    // resolve identically to env-supplied ones.
    {
        std::string base_dir = std::filesystem::path(DEFAULT_CONFIG_PATH).parent_path().string();
        app.learning.ingest_api_log_csv = hecquin::common::resolve_against_dir(
            base_dir, std::move(app.learning.ingest_api_log_csv));
        app.learning.ingest_run_summary_csv = hecquin::common::resolve_against_dir(
            base_dir, std::move(app.learning.ingest_run_summary_csv));
    }

    hecquin::learning::LearningStore store(app.learning.db_path, app.ai.embedding_dim);
    if (!store.open()) {
        std::cerr << "Failed to open learning DB at " << app.learning.db_path << std::endl;
        return 1;
    }

    // Log every outbound embedding POST to the `api_calls` table; optionally
    // tee a CSV row to the configured spend log so cost can be summed without
    // touching the DB.
    hecquin::ai::CurlHttpClient    raw_http;
    auto sink = hecquin::learning::cli::compose_sinks(
        hecquin::learning::cli::make_store_api_call_sink(store),
        hecquin::learning::cli::make_csv_api_call_sink(app.learning.ingest_api_log_csv));
    hecquin::ai::LoggingHttpClient embed_http(raw_http, "embedding", std::move(sink));

    hecquin::learning::EmbeddingClient embedder(app.ai, embed_http);
    if (!embedder.ready()) {
        std::cerr << "Embedding client is not configured "
                  << "(need GEMINI_API_KEY / OPENAI_API_KEY + libcurl)." << std::endl;
        return 1;
    }

    const std::string started_iso = hecquin::learning::cli::utc_iso8601_now();
    const auto t_start = std::chrono::steady_clock::now();

    hecquin::learning::Ingestor ingestor(store, embedder, icfg);
    const auto report = ingestor.run();
    const int exit_code = report.chunks_failed > 0 ? 1 : 0;

    if (!app.learning.ingest_run_summary_csv.empty()) {
        const auto duration =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
        hecquin::learning::cli::RunSummaryRow row;
        row.started_iso      = started_iso;
        row.duration_seconds = duration;
        row.files_scanned    = report.files_scanned;
        row.files_skipped    = report.files_skipped;
        row.files_ingested   = report.files_ingested;
        row.files_pruned     = report.files_pruned;
        row.chunks_written   = report.chunks_written;
        row.chunks_failed    = report.chunks_failed;
        row.exit_code        = exit_code;
        hecquin::learning::cli::append_run_summary(app.learning.ingest_run_summary_csv, row);
    }
    return exit_code;
}
