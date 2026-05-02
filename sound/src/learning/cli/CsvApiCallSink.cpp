#include "learning/cli/CsvApiCallSink.hpp"

#include "learning/cli/CsvEscape.hpp"
#include "learning/cli/RunSummaryCsv.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>

namespace hecquin::learning::cli {

hecquin::ai::ApiCallSink make_csv_api_call_sink(const std::string& path) {
    if (path.empty()) return {};

    auto stream = std::make_shared<std::ofstream>();
    stream->open(path, std::ios::out | std::ios::app);
    if (!stream->is_open()) {
        std::cerr << "[CsvApiCallSink] could not open '" << path << "' for append; "
                  << "API CSV log disabled." << std::endl;
        return {};
    }
    if (stream->tellp() == std::streampos(0)) {
        (*stream) << csv_api_call_header() << '\n';
    }

    auto mtx = std::make_shared<std::mutex>();
    return [stream, mtx](const hecquin::ai::ApiCallRecord& r) {
        std::lock_guard<std::mutex> lock(*mtx);
        std::ostringstream row;
        row << utc_iso8601_now() << ','
            << csv_escape(r.provider) << ','
            << csv_escape(r.endpoint) << ','
            << csv_escape(r.method)   << ','
            << r.status        << ','
            << r.latency_ms    << ','
            << r.request_bytes << ','
            << r.response_bytes << ','
            << (r.ok ? 1 : 0)  << ','
            << csv_escape(r.error)
            << '\n';
        (*stream) << row.str();
        stream->flush();
    };
}

} // namespace hecquin::learning::cli
