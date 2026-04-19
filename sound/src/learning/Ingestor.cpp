#include "learning/Ingestor.hpp"

#include "learning/EmbeddingClient.hpp"
#include "learning/LearningStore.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

namespace hecquin::learning {

namespace {

bool path_exists(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

bool is_dir(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

// FNV-1a 64-bit over (size + content). Sufficient as a change-detection fingerprint.
std::string content_fingerprint(const std::string& content) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : content) {
        h ^= c;
        h *= 1099511628211ull;
    }
    std::ostringstream oss;
    oss << std::hex << content.size() << "-" << h;
    return oss.str();
}

std::string kind_from_dir(const std::string& dir_name) {
    if (dir_name.find("vocab") != std::string::npos) return "vocabulary";
    if (dir_name.find("grammar") != std::string::npos) return "grammar";
    if (dir_name.find("dict") != std::string::npos) return "dictionary";
    if (dir_name.find("reader") != std::string::npos) return "readers";
    return "curriculum";
}

std::string file_basename(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string file_extension(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool is_text_extension(const std::string& ext) {
    return ext == "txt" || ext == "md" || ext == "markdown" ||
           ext == "json" || ext == "jsonl" || ext == "csv" || ext == "tsv" || ext == "text" ||
           ext == "" /* allow files with no extension, e.g. README */;
}

std::vector<std::string> chunk_text(const std::string& text, int chunk_chars, int overlap) {
    std::vector<std::string> out;
    if (text.empty()) return out;
    if (chunk_chars <= 0) chunk_chars = 1800;
    if (overlap < 0) overlap = 0;
    if (overlap >= chunk_chars) overlap = chunk_chars / 4;

    const size_t step = static_cast<size_t>(chunk_chars - overlap);
    for (size_t i = 0; i < text.size(); i += step) {
        size_t end = std::min(text.size(), i + static_cast<size_t>(chunk_chars));
        // Prefer to break on whitespace if possible.
        if (end < text.size()) {
            size_t soft = end;
            while (soft > i + static_cast<size_t>(chunk_chars / 2) &&
                   !std::isspace(static_cast<unsigned char>(text[soft]))) {
                --soft;
            }
            if (soft > i + static_cast<size_t>(chunk_chars / 2)) {
                end = soft;
            }
        }
        std::string piece = text.substr(i, end - i);
        // Trim.
        size_t a = 0, b = piece.size();
        while (a < b && std::isspace(static_cast<unsigned char>(piece[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(piece[b - 1]))) --b;
        if (b > a) out.emplace_back(piece.substr(a, b - a));
        if (end >= text.size()) break;
    }
    return out;
}

void list_files_recursive(const std::string& root, std::vector<std::string>& out) {
    DIR* dir = opendir(root.c_str());
    if (!dir) return;
    while (auto* ent = readdir(dir)) {
        const std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        std::string path = root + "/" + name;
        if (is_dir(path)) {
            list_files_recursive(path, out);
        } else {
            out.push_back(path);
        }
    }
    closedir(dir);
}

} // namespace

Ingestor::Ingestor(LearningStore& store, EmbeddingClient& embed, IngestorConfig cfg)
    : store_(store), embed_(embed), cfg_(std::move(cfg)) {}

namespace {

struct PlannedFile {
    std::string path;
    std::string kind;
};

void collect_files(const std::string& dir, const std::string& default_kind,
                   std::vector<PlannedFile>& out) {
    if (!path_exists(dir)) return;
    std::vector<std::string> files;
    list_files_recursive(dir, files);
    for (const auto& path : files) {
        const std::string ext = file_extension(file_basename(path));
        if (!is_text_extension(ext)) continue;
        std::string kind = default_kind;
        if (kind.empty()) {
            const std::string rel = path.substr(dir.size() + (dir.empty() ? 0 : 1));
            const auto slash = rel.find('/');
            kind = kind_from_dir(slash == std::string::npos ? rel : rel.substr(0, slash));
        }
        out.push_back({path, kind});
    }
}

std::string format_duration(double seconds) {
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

} // namespace

IngestReport Ingestor::run() {
    using clock = std::chrono::steady_clock;
    IngestReport report;

    std::vector<PlannedFile> plan;
    collect_files(cfg_.curriculum_dir, "", plan);
    collect_files(cfg_.custom_dir, "custom", plan);

    if (plan.empty()) {
        std::cerr << "[Ingestor] no files found under '" << cfg_.curriculum_dir << "' or '"
                  << cfg_.custom_dir << "'" << std::endl;
        return report;
    }

    total_files_ = plan.size();
    file_index_ = 0;
    const auto t_start = clock::now();

    std::cerr << "[Ingestor] plan: " << total_files_ << " files "
              << "(curriculum='" << cfg_.curriculum_dir << "', custom='" << cfg_.custom_dir << "')"
              << std::endl;

    for (const auto& f : plan) {
        ++file_index_;
        ingest_file_(f.path, f.kind, report);
    }

    const auto t_end = clock::now();
    const double elapsed = std::chrono::duration<double>(t_end - t_start).count();
    std::cerr << "[Ingestor] done in " << format_duration(elapsed)
              << " — scanned=" << report.files_scanned
              << " ingested=" << report.files_ingested
              << " skipped=" << report.files_skipped
              << " chunks=" << report.chunks_written
              << " failed_chunks=" << report.chunks_failed << std::endl;
    return report;
}

void Ingestor::ingest_dir_(const std::string& dir, const std::string& default_kind,
                           IngestReport& report) {
    // Retained for API compatibility; new code routes through collect_files + run().
    std::vector<PlannedFile> plan;
    collect_files(dir, default_kind, plan);
    for (const auto& f : plan) {
        ++file_index_;
        ingest_file_(f.path, f.kind, report);
    }
}

void Ingestor::ingest_file_(const std::string& path, const std::string& kind,
                            IngestReport& report) {
    using clock = std::chrono::steady_clock;
    ++report.files_scanned;

    const std::string prefix =
        total_files_ > 0
            ? "[" + std::to_string(file_index_) + "/" + std::to_string(total_files_) + "] "
            : std::string();

    const std::string content = read_file(path);
    if (content.empty()) {
        ++report.files_skipped;
        std::cerr << prefix << "skip (empty): " << path << std::endl;
        return;
    }
    const std::string hash = content_fingerprint(content);
    if (!cfg_.force_rebuild && store_.is_file_already_ingested(path, hash)) {
        ++report.files_skipped;
        std::cerr << prefix << "skip (unchanged): " << path << std::endl;
        return;
    }

    const std::string title = file_basename(path);
    const auto chunks = chunk_text(content, cfg_.chunk_chars, cfg_.chunk_overlap_chars);
    if (chunks.empty()) {
        ++report.files_skipped;
        std::cerr << prefix << "skip (no chunks): " << path << std::endl;
        return;
    }

    std::cerr << prefix << "ingesting " << path
              << " (" << chunks.size() << " chunks, kind=" << kind << ", "
              << content.size() << " bytes)" << std::endl;

    const auto t_file_start = clock::now();
    // Emit a heartbeat line at most every ~2s, and always at the first / last chunk.
    auto t_last_tick = t_file_start;
    const double tick_interval_sec = 2.0;

    int ok = 0;
    for (size_t i = 0; i < chunks.size(); ++i) {
        auto emb = embed_.embed(chunks[i]);
        if (!emb) {
            ++report.chunks_failed;
        } else {
            DocumentRecord rec;
            rec.source = path;
            rec.kind = kind;
            rec.title = title + "#" + std::to_string(i + 1);
            rec.body = chunks[i];
            rec.metadata_json = "{\"chunk_index\":" + std::to_string(i) +
                                ",\"chunks_total\":" + std::to_string(chunks.size()) + "}";
            if (store_.upsert_document(rec, *emb)) {
                ++ok;
                ++report.chunks_written;
            } else {
                ++report.chunks_failed;
            }
        }

        const auto now = clock::now();
        const double since_tick = std::chrono::duration<double>(now - t_last_tick).count();
        const bool is_last = (i + 1 == chunks.size());
        if (is_last || since_tick >= tick_interval_sec) {
            const double elapsed = std::chrono::duration<double>(now - t_file_start).count();
            const size_t done = i + 1;
            const double rate = elapsed > 0.0 ? (done / elapsed) : 0.0;
            const double eta =
                (rate > 0.0 && done < chunks.size()) ? (chunks.size() - done) / rate : 0.0;
            const int pct =
                chunks.size() > 0 ? static_cast<int>((100.0 * done) / chunks.size()) : 100;
            std::cerr << prefix << "  chunk " << done << "/" << chunks.size()
                      << " (" << pct << "%) ok=" << ok
                      << " fail=" << report.chunks_failed
                      << " elapsed=" << format_duration(elapsed);
            if (!is_last) std::cerr << " eta=" << format_duration(eta);
            std::cerr << std::endl;
            t_last_tick = now;
        }
    }
    if (ok > 0) {
        store_.record_ingested_file(path, hash);
        ++report.files_ingested;
    }
}

} // namespace hecquin::learning
