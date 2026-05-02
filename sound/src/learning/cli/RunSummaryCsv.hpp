#pragma once

#include <string>

namespace hecquin::learning::cli {

/** One ingest-run worth of totals, ready to land in a CSV row. */
struct RunSummaryRow {
    std::string started_iso;        // ISO-8601 UTC when run() was called
    double duration_seconds = 0.0;
    int files_scanned = 0;
    int files_skipped = 0;
    int files_ingested = 0;
    int files_pruned = 0;
    int chunks_written = 0;
    int chunks_failed = 0;
    int exit_code = 0;
};

/** Append `row` to `path` (no-op if `path` is empty). Writes header if file is new/empty. */
void append_run_summary(const std::string& path, const RunSummaryRow& row);

/** Header line written to a fresh file. Exposed for tests. */
inline const char* run_summary_header() {
    return "started_iso,duration_seconds,files_scanned,files_skipped,"
           "files_ingested,files_pruned,chunks_written,chunks_failed,exit_code";
}

/** UTC ISO-8601 timestamp for `now()`, e.g. "2026-05-02T10:31:00Z". */
std::string utc_iso8601_now();

} // namespace hecquin::learning::cli
