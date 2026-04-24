#include "learning/ingest/FileDiscovery.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace hecquin::learning::ingest {

namespace fs = std::filesystem;

std::string kind_from_dir(const std::string& dir_name) {
    if (dir_name.find("vocab") != std::string::npos)   return "vocabulary";
    if (dir_name.find("grammar") != std::string::npos) return "grammar";
    if (dir_name.find("dict") != std::string::npos)    return "dictionary";
    if (dir_name.find("reader") != std::string::npos)  return "readers";
    return "curriculum";
}

std::string file_extension_lower(const fs::path& p) {
    std::string ext = p.extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool is_text_extension(const std::string& ext) {
    return ext == "txt" || ext == "md" || ext == "markdown" ||
           ext == "json" || ext == "jsonl" || ext == "csv" || ext == "tsv" ||
           ext == "text" ||
           ext.empty(); // allow extensionless files (README, etc.)
}

std::vector<PlannedFile> collect_files(const std::string& dir,
                                       const std::string& default_kind) {
    std::vector<PlannedFile> out;
    std::error_code ec;
    if (dir.empty() || !fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return out;

    const fs::path root = dir;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec),
                                           end;
         it != end;
         it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        const fs::path& p = it->path();
        const std::string ext = file_extension_lower(p);
        if (!is_text_extension(ext)) continue;

        std::string kind = default_kind;
        if (kind.empty()) {
            const fs::path rel = fs::relative(p, root, ec);
            const std::string first =
                rel.empty() || rel.begin() == rel.end()
                    ? std::string{}
                    : rel.begin()->string();
            kind = kind_from_dir(first);
        }
        out.push_back({p.string(), kind});
    }
    return out;
}

} // namespace hecquin::learning::ingest
