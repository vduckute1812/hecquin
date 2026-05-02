// CsvApiCallSink writes the CSV header on the first ever open of a file,
// and only once: subsequent process invocations against the same path
// must append rows without duplicating the header line.

#include "ai/LoggingHttpClient.hpp"
#include "learning/cli/CsvApiCallSink.hpp"

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
    std::cerr << "[test_csv_api_call_sink_header_once] FAIL: " << msg << std::endl;
    return 1;
}

fs::path make_tmp_dir() {
    char buf[] = "/tmp/hecquin_csv_header_XXXXXX";
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

hecquin::ai::ApiCallRecord sample_record() {
    hecquin::ai::ApiCallRecord r;
    r.provider = "gemini";
    r.endpoint = "https://generativelanguage.googleapis.com/v1beta/openai/embeddings";
    r.method = "POST";
    r.status = 200;
    r.latency_ms = 100;
    r.request_bytes = 512;
    r.response_bytes = 1024;
    r.ok = true;
    return r;
}

} // namespace

int main() {
    const fs::path dir = make_tmp_dir();
    if (dir.empty()) return fail("could not create tmp dir");
    const fs::path csv = dir / "api_calls.csv";

    {
        auto sink = hecquin::learning::cli::make_csv_api_call_sink(csv.string());
        if (!sink) return fail("first sink should be non-empty");
        sink(sample_record());
    }
    {
        auto sink = hecquin::learning::cli::make_csv_api_call_sink(csv.string());
        if (!sink) return fail("second sink should be non-empty");
        sink(sample_record());
    }

    const auto lines = read_lines(csv);
    if (lines.size() != 3) {
        std::cerr << "expected header + 2 rows, got " << lines.size() << " lines" << std::endl;
        return fail("wrong line count after second open");
    }
    if (lines[0] != hecquin::learning::cli::csv_api_call_header()) {
        return fail("header missing or wrong");
    }
    if (lines[1] == hecquin::learning::cli::csv_api_call_header() ||
        lines[2] == hecquin::learning::cli::csv_api_call_header()) {
        return fail("header was rewritten on second open");
    }

    fs::remove_all(dir);
    return 0;
}
