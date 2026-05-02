#include "learning/cli/RunSummaryCsv.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace hecquin::learning::cli {

std::string utc_iso8601_now() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto t = clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

void append_run_summary(const std::string& path, const RunSummaryRow& row) {
    if (path.empty()) return;

    std::ofstream out(path, std::ios::out | std::ios::app);
    if (!out.is_open()) {
        std::cerr << "[RunSummaryCsv] could not open '" << path
                  << "' for append; run summary skipped." << std::endl;
        return;
    }
    if (out.tellp() == std::streampos(0)) {
        out << run_summary_header() << '\n';
    }
    out << row.started_iso << ','
        << std::fixed << std::setprecision(3) << row.duration_seconds << ','
        << row.files_scanned  << ','
        << row.files_skipped  << ','
        << row.files_ingested << ','
        << row.files_pruned   << ','
        << row.chunks_written << ','
        << row.chunks_failed  << ','
        << row.exit_code << '\n';
}

} // namespace hecquin::learning::cli
