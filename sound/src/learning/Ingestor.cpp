#include "learning/Ingestor.hpp"

#include "common/Utf8.hpp"
#include "learning/ingest/ChunkingStrategy.hpp"
#include "learning/ingest/ContentFingerprint.hpp"
#include "learning/ingest/DocumentPersister.hpp"
#include "learning/ingest/EmbeddingBatcher.hpp"
#include "learning/ingest/FileDiscovery.hpp"
#include "learning/ingest/ProgressReporter.hpp"
#include "learning/store/LearningStore.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

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

} // namespace

Ingestor::Ingestor(LearningStore& store, EmbeddingClient& embed, IngestorConfig cfg)
    : store_(store), embed_(embed), cfg_(std::move(cfg)) {}

IngestReport Ingestor::run() {
    using Clock = ingest::ProgressReporter::Clock;
    IngestReport report;
    ingest::ProgressReporter reporter;

    std::vector<ingest::PlannedFile> plan;
    {
        auto curriculum = ingest::collect_files(cfg_.curriculum_dir, "");
        plan.insert(plan.end(),
                    std::make_move_iterator(curriculum.begin()),
                    std::make_move_iterator(curriculum.end()));
        auto custom = ingest::collect_files(cfg_.custom_dir, "custom");
        plan.insert(plan.end(),
                    std::make_move_iterator(custom.begin()),
                    std::make_move_iterator(custom.end()));
    }

    if (plan.empty()) {
        std::cerr << "[Ingestor] no files found under '" << cfg_.curriculum_dir << "' or '"
                  << cfg_.custom_dir << "'" << std::endl;
        return report;
    }

    total_files_ = plan.size();
    file_index_ = 0;
    const auto t_start = Clock::now();

    reporter.begin_plan(total_files_, cfg_.curriculum_dir, cfg_.custom_dir);

    for (const auto& f : plan) {
        ++file_index_;
        ingest_file_(f.path, f.kind, report);
    }

    reporter.finish_plan(report, t_start);
    return report;
}

void Ingestor::ingest_file_(const std::string& path, const std::string& kind,
                            IngestReport& report) {
    using Clock = ingest::ProgressReporter::Clock;
    ingest::ProgressReporter reporter;
    ++report.files_scanned;

    std::string content = read_file(path);
    if (content.empty()) {
        ++report.files_skipped;
        reporter.skip_empty(file_index_, total_files_, path);
        return;
    }

    // Scrub non-UTF-8 bytes (e.g. CP-1252 NBSP 0xA0 in vocabulary CSVs) before
    // anything downstream sees them. nlohmann::json::dump(), SQLite's TEXT
    // affinity, and the embedding API all assume valid UTF-8.
    content = hecquin::common::sanitize_utf8(content);
    const std::string hash = ingest::content_fingerprint(content);
    if (!cfg_.force_rebuild && store_.is_file_already_ingested(path, hash)) {
        ++report.files_skipped;
        reporter.skip_unchanged(file_index_, total_files_, path);
        return;
    }

    const std::string title = fs::path(path).filename().string();
    const std::string ext = ingest::file_extension_lower(fs::path(path));
    auto chunker = ingest::make_chunker_for_extension(ext, cfg_.chunk_chars, cfg_.chunk_overlap_chars);
    const auto chunks = chunker->chunk(content);
    if (chunks.empty()) {
        ++report.files_skipped;
        reporter.skip_no_chunks(file_index_, total_files_, path);
        return;
    }

    reporter.begin_file(file_index_, total_files_, path, chunks.size(), content.size(), kind);

    ingest::EmbeddingBatcher batcher(embed_, cfg_.embed_batch_size);
    ingest::DocumentPersister persister(store_);
    ingest::DocumentPersister::FileParams pp;
    pp.source = path;
    pp.kind = kind;
    pp.title_prefix = title;
    pp.total_chunks = chunks.size();

    const auto t_file_start = Clock::now();
    const int batch_size = batcher.batch_size();
    int ok = 0;

    for (std::size_t i = 0; i < chunks.size(); i += static_cast<std::size_t>(batch_size)) {
        const std::size_t end = std::min(chunks.size(), i + static_cast<std::size_t>(batch_size));
        const std::vector<std::string> slice(chunks.begin() + i, chunks.begin() + end);

        const auto embeddings = batcher.embed_slice(slice);
        for (std::size_t j = 0; j < slice.size(); ++j) {
            if (!embeddings[j]) {
                ++report.chunks_failed;
                continue;
            }
            if (persister.persist(pp, i + j, slice[j], *embeddings[j])) {
                ++ok;
                ++report.chunks_written;
            } else {
                ++report.chunks_failed;
            }
        }

        reporter.chunk_progress(file_index_, total_files_, end, chunks.size(),
                                ok, report.chunks_failed, t_file_start);
    }

    if (ok > 0) {
        store_.record_ingested_file(path, hash);
        ++report.files_ingested;
    }
}

} // namespace hecquin::learning
