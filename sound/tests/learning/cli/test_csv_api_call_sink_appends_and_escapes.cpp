// CsvApiCallSink writes the header on a fresh file, then one CSV row per
// ApiCallRecord, with RFC-4180 quoting for fields containing commas/quotes.

#include "ai/LoggingHttpClient.hpp"
#include "learning/cli/CsvApiCallSink.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int fail(const char* msg) {
    std::cerr << "[test_csv_api_call_sink_appends] FAIL: " << msg << std::endl;
    return 1;
}

fs::path make_tmp_dir() {
    char buf[] = "/tmp/hecquin_csv_sink_XXXXXX";
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

bool field_count_is(const std::string& line, std::size_t expected) {
    // Crude check: count commas outside of quoted regions.
    std::size_t fields = 1;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') { ++i; continue; }
            in_quotes = !in_quotes;
        } else if (c == ',' && !in_quotes) {
            ++fields;
        }
    }
    return fields == expected;
}

} // namespace

int main() {
    const fs::path dir = make_tmp_dir();
    if (dir.empty()) return fail("could not create tmp dir");
    const fs::path csv = dir / "api_calls.csv";

    auto sink = hecquin::learning::cli::make_csv_api_call_sink(csv.string());
    if (!sink) return fail("sink should be non-empty for a writable path");

    hecquin::ai::ApiCallRecord r1;
    r1.provider = "openai";
    r1.endpoint = "https://api.openai.com/v1/embeddings";
    r1.method = "POST";
    r1.status = 200;
    r1.latency_ms = 142;
    r1.request_bytes = 1024;
    r1.response_bytes = 2048;
    r1.ok = true;
    r1.error = "";

    hecquin::ai::ApiCallRecord r2 = r1;
    r2.status = 403;
    r2.ok = false;
    r2.error = "rate limited, please \"retry\" later";

    sink(r1);
    sink(r2);

    auto lines = read_lines(csv);
    if (lines.size() != 3) {
        std::cerr << "expected 3 lines (header + 2 rows), got " << lines.size() << std::endl;
        return fail("wrong line count");
    }
    if (lines[0] != hecquin::learning::cli::csv_api_call_header()) {
        std::cerr << "header mismatch: '" << lines[0] << "'" << std::endl;
        return fail("header line wrong");
    }

    constexpr std::size_t kExpectedFields = 10;
    if (!field_count_is(lines[1], kExpectedFields)) return fail("row 1 field count");
    if (!field_count_is(lines[2], kExpectedFields)) return fail("row 2 field count");

    if (lines[2].find("\"rate limited, please \"\"retry\"\" later\"") == std::string::npos) {
        std::cerr << "row 2 was: " << lines[2] << std::endl;
        return fail("CSV escaping for embedded comma + quotes is wrong");
    }

    fs::remove_all(dir);
    return 0;
}
