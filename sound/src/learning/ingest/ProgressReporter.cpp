#include "learning/ingest/ProgressReporter.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace hecquin::learning::ingest {

std::string ProgressReporter::format_duration(double seconds) {
    std::ostringstream oss;
    if (seconds < 60.0) {
        oss << std::fixed << std::setprecision(1) << seconds << "s";
    } else if (seconds < 3600.0) {
        const int m = static_cast<int>(seconds / 60);
        const int s = static_cast<int>(seconds) % 60;
        oss << m << "m" << std::setw(2) << std::setfill('0') << s << "s";
    } else {
        const int h = static_cast<int>(seconds / 3600);
        const int m = static_cast<int>(seconds / 60) % 60;
        oss << h << "h" << std::setw(2) << std::setfill('0') << m << "m";
    }
    return oss.str();
}

std::string ProgressReporter::prefix_(std::size_t file_index, std::size_t total_files) {
    if (total_files == 0) return {};
    return "[" + std::to_string(file_index) + "/" + std::to_string(total_files) + "] ";
}

void ProgressReporter::begin_plan(std::size_t total_files,
                                  const std::string& curriculum_dir,
                                  const std::string& custom_dir) {
    std::cerr << "[Ingestor] plan: " << total_files << " files "
              << "(curriculum='" << curriculum_dir << "', custom='" << custom_dir << "')"
              << std::endl;
}

void ProgressReporter::finish_plan(const IngestReport& report, Clock::time_point t_start) {
    const auto t_end = Clock::now();
    const double elapsed = std::chrono::duration<double>(t_end - t_start).count();
    std::cerr << "[Ingestor] done in " << format_duration(elapsed)
              << " — scanned=" << report.files_scanned
              << " ingested=" << report.files_ingested
              << " skipped=" << report.files_skipped
              << " chunks=" << report.chunks_written
              << " failed_chunks=" << report.chunks_failed << std::endl;
}

void ProgressReporter::begin_file(std::size_t file_index, std::size_t total_files,
                                  const std::string& path,
                                  std::size_t chunks,
                                  std::size_t content_bytes,
                                  const std::string& kind) {
    std::cerr << prefix_(file_index, total_files) << "ingesting " << path
              << " (" << chunks << " chunks, kind=" << kind << ", "
              << content_bytes << " bytes)" << std::endl;
}

void ProgressReporter::skip_empty(std::size_t file_index, std::size_t total_files,
                                  const std::string& path) {
    std::cerr << prefix_(file_index, total_files) << "skip (empty): " << path << std::endl;
}

void ProgressReporter::skip_unchanged(std::size_t file_index, std::size_t total_files,
                                      const std::string& path) {
    std::cerr << prefix_(file_index, total_files) << "skip (unchanged): " << path << std::endl;
}

void ProgressReporter::skip_no_chunks(std::size_t file_index, std::size_t total_files,
                                      const std::string& path) {
    std::cerr << prefix_(file_index, total_files) << "skip (no chunks): " << path << std::endl;
}

void ProgressReporter::chunk_progress(std::size_t file_index, std::size_t total_files,
                                      std::size_t done, std::size_t total,
                                      int ok, int fail,
                                      Clock::time_point t_file_start) {
    const auto now = Clock::now();
    const double elapsed = std::chrono::duration<double>(now - t_file_start).count();
    const double rate = elapsed > 0.0 ? (static_cast<double>(done) / elapsed) : 0.0;
    const double eta =
        (rate > 0.0 && done < total) ? (static_cast<double>(total - done) / rate) : 0.0;
    const int pct = total > 0 ? static_cast<int>((100.0 * static_cast<double>(done)) / static_cast<double>(total)) : 100;
    const bool is_last = (done == total);
    std::cerr << prefix_(file_index, total_files) << "  chunk " << done << "/" << total
              << " (" << pct << "%) ok=" << ok
              << " fail=" << fail
              << " elapsed=" << format_duration(elapsed);
    if (!is_last) std::cerr << " eta=" << format_duration(eta);
    std::cerr << std::endl;
}

} // namespace hecquin::learning::ingest
