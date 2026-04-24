#include "learning/Ingestor.hpp"

#include "common/Utf8.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/store/LearningStore.hpp"
#include "learning/TextChunker.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>

namespace hecquin::learning {

namespace fs = std::filesystem;

namespace {

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
    if (dir_name.find("vocab")   != std::string::npos) return "vocabulary";
    if (dir_name.find("grammar") != std::string::npos) return "grammar";
    if (dir_name.find("dict")    != std::string::npos) return "dictionary";
    if (dir_name.find("reader")  != std::string::npos) return "readers";
    return "curriculum";
}

/** Lower-cased file extension without the leading '.'. */
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
           ext.empty();  // allow extensionless files (README, etc.)
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
    std::error_code ec;
    if (dir.empty() || !fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;

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

    std::string content = read_file(path);
    if (content.empty()) {
        ++report.files_skipped;
        std::cerr << prefix << "skip (empty): " << path << std::endl;
        return;
    }
    // Scrub non-UTF-8 bytes (e.g. CP-1252 NBSP 0xA0 in vocabulary CSVs) before
    // anything downstream sees them. nlohmann::json::dump(), SQLite's TEXT
    // affinity, and the embedding API all assume valid UTF-8; letting raw
    // Latin-1 bytes through used to abort the ingest with a json::type_error.
    content = hecquin::common::sanitize_utf8(content);
    const std::string hash = content_fingerprint(content);
    if (!cfg_.force_rebuild && store_.is_file_already_ingested(path, hash)) {
        ++report.files_skipped;
        std::cerr << prefix << "skip (unchanged): " << path << std::endl;
        return;
    }

    const std::string title = fs::path(path).filename().string();
    const std::string ext = file_extension_lower(fs::path(path));
    const auto chunks = (ext == "jsonl" || ext == "json")
        ? chunk_lines(content, cfg_.chunk_chars)
        : chunk_text(content, cfg_.chunk_chars, cfg_.chunk_overlap_chars);
    if (chunks.empty()) {
        ++report.files_skipped;
        std::cerr << prefix << "skip (no chunks): " << path << std::endl;
        return;
    }

    std::cerr << prefix << "ingesting " << path
              << " (" << chunks.size() << " chunks, kind=" << kind << ", "
              << content.size() << " bytes)" << std::endl;

    const auto t_file_start = clock::now();
    const int batch_size = std::max(1, cfg_.embed_batch_size);

    // Persist one batch + write its rows; returns #successful upserts.
    auto persist_batch = [&](size_t batch_start, const std::vector<std::string>& slice,
                             const std::vector<std::vector<float>>& embeddings,
                             int& ok_ref) {
        for (size_t j = 0; j < slice.size(); ++j) {
            DocumentRecord rec;
            rec.source = path;
            rec.kind = kind;
            rec.title = title + "#" + std::to_string(batch_start + j + 1);
            rec.body = slice[j];
            rec.metadata_json = "{\"chunk_index\":" + std::to_string(batch_start + j) +
                                ",\"chunks_total\":" + std::to_string(chunks.size()) + "}";
            if (store_.upsert_document(rec, embeddings[j])) {
                ++ok_ref;
                ++report.chunks_written;
            } else {
                ++report.chunks_failed;
            }
        }
    };

    int ok = 0;
    for (size_t i = 0; i < chunks.size(); i += static_cast<size_t>(batch_size)) {
        const size_t end = std::min(chunks.size(), i + static_cast<size_t>(batch_size));
        const std::vector<std::string> slice(chunks.begin() + i, chunks.begin() + end);

        const auto batch_embed = embed_.embed_many(slice);
        if (batch_embed && batch_embed->size() == slice.size()) {
            persist_batch(i, slice, *batch_embed, ok);
        } else {
            // Fall back to per-chunk so a single bad chunk doesn't tank the file.
            for (size_t j = 0; j < slice.size(); ++j) {
                auto single = embed_.embed(slice[j]);
                if (!single) {
                    ++report.chunks_failed;
                    continue;
                }
                persist_batch(i + j, {slice[j]}, {*single}, ok);
            }
        }

        const auto now = clock::now();
        const double elapsed = std::chrono::duration<double>(now - t_file_start).count();
        const size_t done = end;
        const double rate = elapsed > 0.0 ? (done / elapsed) : 0.0;
        const double eta =
            (rate > 0.0 && done < chunks.size()) ? (chunks.size() - done) / rate : 0.0;
        const int pct =
            chunks.size() > 0 ? static_cast<int>((100.0 * done) / chunks.size()) : 100;
        const bool is_last = (done == chunks.size());
        std::cerr << prefix << "  chunk " << done << "/" << chunks.size()
                  << " (" << pct << "%) ok=" << ok
                  << " fail=" << report.chunks_failed
                  << " elapsed=" << format_duration(elapsed);
        if (!is_last) std::cerr << " eta=" << format_duration(eta);
        std::cerr << std::endl;
    }
    if (ok > 0) {
        store_.record_ingested_file(path, hash);
        ++report.files_ingested;
    }
}

} // namespace hecquin::learning
