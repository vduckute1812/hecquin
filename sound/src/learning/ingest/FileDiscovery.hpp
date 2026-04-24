#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace hecquin::learning::ingest {

/** One planned input file with the subject kind it should be ingested under. */
struct PlannedFile {
    std::string path;
    std::string kind;
};

/**
 * Walk `dir` recursively, filter for known text extensions, and assign
 * a `kind` per file.  When `default_kind` is empty, the kind is derived
 * from the file's top-level directory name (see `kind_from_dir`).
 */
std::vector<PlannedFile> collect_files(const std::string& dir,
                                       const std::string& default_kind);

/** Map a top-level directory name to a document `kind`. */
std::string kind_from_dir(const std::string& dir_name);

/** Lower-cased file extension without the leading '.'. */
std::string file_extension_lower(const std::filesystem::path& p);

/** Allow-list for extensions the ingestor is willing to read. */
bool is_text_extension(const std::string& ext);

} // namespace hecquin::learning::ingest
