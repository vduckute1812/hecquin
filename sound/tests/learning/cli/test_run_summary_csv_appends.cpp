// append_run_summary writes a header on first call and one data row per
// invocation; second call against the same path appends without
// duplicating the header.

#include "learning/cli/RunSummaryCsv.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int fail(const char* msg) {
    std::cerr << "[test_run_summary_csv_appends] FAIL: " << msg << std::endl;
    return 1;
}

fs::path make_tmp_dir() {
    char buf[] = "/tmp/hecquin_run_summary_XXXXXX";
    if (!mkdtemp(buf)) return {};
    return fs::path(buf);
}

std::vector<std::string> read_lines(const fs::path& p) {
    std::vector<std::string> out;
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line)) out.push_back(line);
    return out;
}

hecquin::learning::cli::RunSummaryRow make_row(int chunks_written, int exit_code) {
    hecquin::learning::cli::RunSummaryRow row;
    row.started_iso      = "2026-05-02T10:31:00Z";
    row.duration_seconds = 12.345;
    row.files_scanned    = 4;
    row.files_skipped    = 1;
    row.files_ingested   = 3;
    row.files_pruned     = 0;
    row.chunks_written   = chunks_written;
    row.chunks_failed    = 0;
    row.exit_code        = exit_code;
    return row;
}

} // namespace

int main() {
    const fs::path dir = make_tmp_dir();
    if (dir.empty()) return fail("could not create tmp dir");
    const fs::path csv = dir / "runs.csv";

    hecquin::learning::cli::append_run_summary(csv.string(), make_row(42, 0));
    hecquin::learning::cli::append_run_summary(csv.string(), make_row(7, 1));

    const auto lines = read_lines(csv);
    if (lines.size() != 3) {
        std::cerr << "expected header + 2 rows, got " << lines.size() << " lines" << std::endl;
        return fail("wrong line count");
    }
    if (lines[0] != hecquin::learning::cli::run_summary_header()) {
        return fail("header line wrong");
    }
    if (lines[1].find("2026-05-02T10:31:00Z,12.345,4,1,3,0,42,0,0") == std::string::npos) {
        std::cerr << "row 1 was: " << lines[1] << std::endl;
        return fail("row 1 contents mismatch");
    }
    if (lines[2].find(",7,0,1") == std::string::npos) {
        std::cerr << "row 2 was: " << lines[2] << std::endl;
        return fail("row 2 (chunks_written=7, chunks_failed=0, exit_code=1) wrong");
    }

    // Empty path: silent no-op, must not crash.
    hecquin::learning::cli::append_run_summary("", make_row(1, 0));

    fs::remove_all(dir);
    return 0;
}
