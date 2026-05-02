#include "learning/Ingestor.hpp"

#include "common/Utf8.hpp"
#include "learning/EmbeddingClient.hpp"
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
#include <unordered_set>
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

// Stable absolute path used as DB identity across cwds / `..` spellings.
std::string canonical_identity(const std::string& path) {
    std::error_code ec;
    auto canonical = fs::weakly_canonical(fs::path(path), ec);
    if (ec) return path;
    return canonical.string();
}

std::vector<ingest::PlannedFile> collect_plan(const IngestorConfig& cfg) {
    std::vector<ingest::PlannedFile> plan;
    auto curriculum = ingest::collect_files(cfg.curriculum_dir, "");
    plan.insert(plan.end(),
                std::make_move_iterator(curriculum.begin()),
                std::make_move_iterator(curriculum.end()));
    // Empty default_kind so subdirs under custom/ auto-tag like curriculum/.
    auto custom = ingest::collect_files(cfg.custom_dir, "");
    plan.insert(plan.end(),
                std::make_move_iterator(custom.begin()),
                std::make_move_iterator(custom.end()));
    return plan;
}

} // namespace

Ingestor::Ingestor(LearningStore& store, EmbeddingClient& embed, IngestorConfig cfg)
    : store_(store), embed_(embed), cfg_(std::move(cfg)) {}

bool Ingestor::probe_embedding_dim_() {
    const auto probe = embed_.embed_many_classified({"hecquin-dim-probe"});
    if (probe.dim_mismatch && probe.vectors == std::nullopt) {
        std::cerr << "[Ingestor] embedding API dimension mismatch: API returned "
                  << "vectors of a different size than HECQUIN_AI_EMBEDDING_DIM. "
                  << "Update HECQUIN_AI_EMBEDDING_DIM in sound/.env/config.env to "
                  << "match the provider, then drop the existing learning DB before "
                  << "re-ingesting." << std::endl;
        return false;
    }
    if (probe.vectors && !probe.vectors->empty()) {
        const int got = static_cast<int>(probe.vectors->front().size());
        const int want = embed_.config().embedding_dim;
        if (want > 0 && got != want) {
            std::cerr << "[Ingestor] embedding API dimension mismatch: got dim=" << got
                      << ", configured HECQUIN_AI_EMBEDDING_DIM=" << want << ".\n"
                      << "  Set HECQUIN_AI_EMBEDDING_DIM=" << got
                      << " in sound/.env/config.env (and drop the existing DB)."
                      << std::endl;
            return false;
        }
    }
    // Soft failures fall through; the per-file path will surface them in context.
    return true;
}

IngestReport Ingestor::run() {
    using Clock = ingest::ProgressReporter::Clock;
    IngestReport report;
    ingest::ProgressReporter reporter;

    auto plan = collect_plan(cfg_);
    if (plan.empty()) {
        std::cerr << "[Ingestor] no files found under '" << cfg_.curriculum_dir << "' or '"
                  << cfg_.custom_dir << "'" << std::endl;
        return report;
    }

    if (!probe_embedding_dim_()) return report;

    total_files_ = plan.size();
    file_index_ = 0;
    const auto t_start = Clock::now();
    const auto deadline = cfg_.run_deadline_seconds > 0
        ? std::optional<Clock::time_point>(t_start + std::chrono::seconds(cfg_.run_deadline_seconds))
        : std::nullopt;
    breaker_.emplace(cfg_.max_consecutive_chunk_failures, deadline);

    reporter.begin_plan(total_files_, cfg_.curriculum_dir, cfg_.custom_dir);

    for (const auto& f : plan) {
        if (breaker_->tripped()) break;
        if (breaker_->check_deadline()) break;
        ++file_index_;
        ingest_file_(f.path, f.kind, report);
    }

    if (breaker_->tripped()) {
        std::cerr << "[Ingestor] aborted: " << breaker_->reason() << std::endl;
    }
    if (cfg_.prune_missing_sources && !breaker_->tripped()) {
        prune_missing_sources_(plan, report);
    }

    reporter.finish_plan(report, t_start);
    return report;
}

void Ingestor::ingest_file_(const std::string& path, const std::string& kind,
                            IngestReport& report) {
    const int chunks_failed_at_entry = report.chunks_failed;
    auto plan = prepare_file_(path, kind, report);
    if (!plan) return;

    ingest::ProgressReporter reporter;
    reporter.begin_file(file_index_, total_files_, plan->path,
                        plan->chunks.size(), plan->content_size, plan->kind);

    // Atomic per-file replace — kills orphan chunks from any prior shrink.
    store_.purge_documents_for_source(plan->source_id);

    const auto outcome = embed_and_persist_(*plan, report);
    commit_or_rollback_(*plan, outcome, chunks_failed_at_entry, report);
}

std::optional<Ingestor::FilePlan>
Ingestor::prepare_file_(const std::string& path, const std::string& kind,
                        IngestReport& report) {
    ingest::ProgressReporter reporter;
    ++report.files_scanned;

    const std::string source_id = canonical_identity(path);

    // Stat-then-skip so a pathological dump can't OOM the read buffer.
    if (cfg_.max_file_bytes > 0) {
        std::error_code ec;
        const auto sz = fs::file_size(path, ec);
        if (!ec && static_cast<long long>(sz) > cfg_.max_file_bytes) {
            ++report.files_skipped;
            std::cerr << "[Ingestor] skip (too large): " << path << " ("
                      << sz << " bytes > max_file_bytes=" << cfg_.max_file_bytes << ")"
                      << std::endl;
            return std::nullopt;
        }
    }

    std::string content = read_file(path);
    if (content.empty()) {
        ++report.files_skipped;
        reporter.skip_empty(file_index_, total_files_, path);
        return std::nullopt;
    }

    // Downstream (json::dump, SQLite TEXT, embedding API) all require valid UTF-8.
    content = hecquin::common::sanitize_utf8(content);
    const std::string hash = ingest::content_fingerprint(content);
    if (!cfg_.force_rebuild && store_.is_file_already_ingested(source_id, hash)) {
        ++report.files_skipped;
        reporter.skip_unchanged(file_index_, total_files_, path);
        return std::nullopt;
    }

    const std::string ext = ingest::file_extension_lower(fs::path(path));
    auto chunker = ingest::make_chunker_for_extension(
        ext, cfg_.chunk_chars, cfg_.chunk_overlap_chars);
    auto chunks = chunker->chunk(content);
    if (chunks.empty()) {
        ++report.files_skipped;
        reporter.skip_no_chunks(file_index_, total_files_, path);
        return std::nullopt;
    }

    return FilePlan{
        path,
        source_id,
        fs::path(path).filename().string(),
        kind,
        hash,
        std::move(chunks),
        content.size(),
    };
}

Ingestor::ChunkOutcome
Ingestor::embed_and_persist_(const FilePlan& plan, IngestReport& report) {
    using Clock = ingest::ProgressReporter::Clock;
    ingest::ProgressReporter reporter;

    ingest::EmbeddingBatcher batcher(embed_, cfg_.embed_batch_size);
    ingest::DocumentPersister persister(store_);
    ingest::DocumentPersister::FileParams pp;
    pp.source = plan.source_id;
    pp.kind = plan.kind;
    pp.title_prefix = plan.title;
    pp.total_chunks = plan.chunks.size();

    const auto t_file_start = Clock::now();
    const int batch_size = batcher.batch_size();
    ChunkOutcome out;

    for (std::size_t i = 0; i < plan.chunks.size(); i += static_cast<std::size_t>(batch_size)) {
        if (breaker_ && breaker_->tripped()) break;
        const std::size_t end =
            std::min(plan.chunks.size(), i + static_cast<std::size_t>(batch_size));
        const std::vector<std::string> slice(plan.chunks.begin() + i,
                                             plan.chunks.begin() + end);

        const auto embeddings = batcher.embed_slice(slice);
        for (std::size_t j = 0; j < slice.size(); ++j) {
            const bool persisted = embeddings[j] &&
                                   persister.persist(pp, i + j, slice[j], *embeddings[j]);
            if (persisted) {
                ++out.ok;
                ++report.chunks_written;
                if (breaker_) breaker_->record_success();
            } else {
                ++out.failed;
                ++report.chunks_failed;
                if (breaker_ && breaker_->record_failure()) break;
            }
        }

        reporter.chunk_progress(file_index_, total_files_, end, plan.chunks.size(),
                                out.ok, report.chunks_failed, t_file_start);
    }
    return out;
}

void Ingestor::commit_or_rollback_(const FilePlan& plan, ChunkOutcome outcome,
                                   int chunks_failed_at_entry, IngestReport& report) {
    // Mark ingested only on full success, else roll back so a re-run retries.
    const bool full_success = outcome.ok > 0 &&
                              outcome.failed == 0 &&
                              report.chunks_failed == chunks_failed_at_entry;
    if (full_success) {
        store_.record_ingested_file(plan.source_id, plan.hash);
        ++report.files_ingested;
        return;
    }
    if (outcome.ok > 0) {
        store_.purge_documents_for_source(plan.source_id);
        report.chunks_written -= outcome.ok;
        std::cerr << "[Ingestor] partial failure (" << outcome.failed << " chunk(s) of "
                  << plan.chunks.size() << "); rolled back '" << plan.path
                  << "' — re-run will retry." << std::endl;
    }
}

void Ingestor::prune_missing_sources_(const std::vector<ingest::PlannedFile>& plan,
                                      IngestReport& report) {
    // Reap DB rows whose source is not in the current plan (file removed from disk).
    std::unordered_set<std::string> planned;
    planned.reserve(plan.size());
    for (const auto& f : plan) {
        planned.insert(canonical_identity(f.path));
    }

    const auto known_sources = store_.list_document_sources();
    int pruned = 0;
    for (const auto& src : known_sources) {
        if (planned.count(src)) continue;
        const int rows = store_.purge_documents_for_source(src);
        store_.delete_ingested_file(src);
        ++pruned;
        std::cerr << "[Ingestor] pruned '" << src << "' (" << rows
                  << " chunk(s) removed)" << std::endl;
    }
    report.files_pruned = pruned;
}

} // namespace hecquin::learning
