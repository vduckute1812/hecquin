#pragma once

#include "learning/Ingestor.hpp"

#include <chrono>
#include <cstddef>
#include <string>

namespace hecquin::learning::ingest {

/**
 * CLI progress + ETA reporter.  Collects all human-readable ingestor
 * output in one place so the orchestrator doesn't interleave domain
 * logic with stderr lines.
 */
class ProgressReporter {
public:
    using Clock = std::chrono::steady_clock;

    /** Duration formatter shared by the reporter and callers. */
    static std::string format_duration(double seconds);

    void begin_plan(std::size_t total_files,
                    const std::string& curriculum_dir,
                    const std::string& custom_dir);

    void finish_plan(const IngestReport& report, Clock::time_point t_start);

    void begin_file(std::size_t file_index, std::size_t total_files,
                    const std::string& path,
                    std::size_t chunks,
                    std::size_t content_bytes,
                    const std::string& kind);

    void skip_empty(std::size_t file_index, std::size_t total_files,
                    const std::string& path);
    void skip_unchanged(std::size_t file_index, std::size_t total_files,
                        const std::string& path);
    void skip_no_chunks(std::size_t file_index, std::size_t total_files,
                        const std::string& path);

    void chunk_progress(std::size_t file_index, std::size_t total_files,
                        std::size_t done, std::size_t total,
                        int ok, int fail,
                        Clock::time_point t_file_start);

private:
    static std::string prefix_(std::size_t file_index, std::size_t total_files);
};

} // namespace hecquin::learning::ingest
