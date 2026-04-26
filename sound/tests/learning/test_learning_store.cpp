#include "learning/store/LearningStore.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

int fail(const char* message) {
    std::cerr << "[test_learning_store] FAIL: " << message << std::endl;
    return 1;
}

/** Write to a unique tmp path; caller `unlink`s after. */
std::string make_tmp_db() {
    char buf[] = "/tmp/hecquin_learning_XXXXXX";
    const int fd = mkstemp(buf);
    if (fd < 0) return {};
    close(fd);
    // mkstemp leaves an empty file; remove so SQLite creates its own.
    std::remove(buf);
    return std::string(buf) + ".sqlite";
}

std::vector<float> fake_vec(int dim, float seed) {
    std::vector<float> v(dim, 0.0f);
    for (int i = 0; i < dim; ++i) v[i] = seed + static_cast<float>(i) * 0.01f;
    return v;
}

} // namespace

int main() {
    using namespace hecquin::learning;

    const int dim = 8;
    const std::string db = make_tmp_db();
    if (db.empty()) return fail("could not allocate tmp db path");

    {
        LearningStore store(db, dim);
        if (!store.open()) {
            std::remove(db.c_str());
            return fail("open() failed for fresh DB");
        }

        DocumentRecord a;
        a.source = "unit-test";
        a.kind = "grammar";
        a.title = "alpha";
        a.body = "first body";
        const auto rid_a = store.upsert_document(a, fake_vec(dim, 1.0f));
        if (!rid_a) return fail("upsert #1");

        // Upserting the same identity (source+kind+title) should succeed and
        // replace the previous row — top-1 query should still return this doc.
        DocumentRecord a2 = a;
        a2.body = "rewritten body";
        const auto rid_a2 = store.upsert_document(a2, fake_vec(dim, 1.5f));
        if (!rid_a2) return fail("upsert #1 again");

        DocumentRecord b;
        b.source = "unit-test";
        b.kind = "vocabulary";
        b.title = "beta";
        b.body = "second body";
        if (!store.upsert_document(b, fake_vec(dim, -1.0f))) return fail("upsert #2");

        const auto top = store.query_top_k(fake_vec(dim, 1.5f), 1);
        if (top.size() != 1) return fail("top_k size");
        if (top[0].doc.title != "alpha") return fail("top_k picks closest doc");
        if (top[0].doc.body != "rewritten body") return fail("upsert replaced body");

        store.record_ingested_file("/tmp/foo.txt", "h1");
        if (!store.is_file_already_ingested("/tmp/foo.txt", "h1")) {
            return fail("record + lookup");
        }
        // Different hash for the same path must NOT match.
        if (store.is_file_already_ingested("/tmp/foo.txt", "h2")) {
            return fail("stale-hash lookup must miss");
        }
    }

    // Reopen with a mismatching dim — store must refuse.
    {
        LearningStore wrong_dim(db, dim + 1);
        if (wrong_dim.open()) {
            std::remove(db.c_str());
            return fail("reopen with wrong dim should fail");
        }
    }

    // Reopen with the correct dim — store must succeed and see the old row.
    {
        LearningStore store(db, dim);
        if (!store.open()) {
            std::remove(db.c_str());
            return fail("reopen with correct dim");
        }
        if (!store.is_file_already_ingested("/tmp/foo.txt", "h1")) {
            std::remove(db.c_str());
            return fail("data persisted across reopen");
        }
    }

    // weakest_phonemes: ranks by avg_score ascending, filters by min_attempts.
    {
        LearningStore store(db, dim);
        if (!store.open()) {
            std::remove(db.c_str());
            return fail("open for mastery ranking");
        }
        // "θ" very weak, observed 3x; "s" mediocre, 2x; "ɑ" decent, 1x only →
        // should be filtered out by min_attempts=2.
        store.touch_phoneme_mastery({{"θ", 10.0f}, {"s", 55.0f}, {"ɑ", 90.0f}});
        store.touch_phoneme_mastery({{"θ", 12.0f}, {"s", 60.0f}});
        store.touch_phoneme_mastery({{"θ", 15.0f}});

        const auto weak = store.weakest_phonemes(/*n=*/5, /*min_attempts=*/2);
        if (weak.size() != 2) {
            std::remove(db.c_str());
            return fail("weakest_phonemes should return exactly two rows");
        }
        if (weak[0] != "θ" || weak[1] != "s") {
            std::remove(db.c_str());
            return fail("weakest_phonemes should sort by avg_score ascending");
        }

        // limit argument honoured.
        const auto top1 = store.weakest_phonemes(1, 2);
        if (top1.size() != 1 || top1[0] != "θ") {
            std::remove(db.c_str());
            return fail("weakest_phonemes(1) should return only the worst");
        }
    }

    // pipeline_events: round-trip one VAD skip and one Whisper ok.
    {
        LearningStore store(db, dim);
        if (!store.open()) {
            std::remove(db.c_str());
            return fail("open for pipeline_events");
        }
        store.record_pipeline_event("vad_gate", "skipped", 420,
                                    R"({"reason":"too_quiet"})");
        store.record_pipeline_event("whisper", "ok", 140, "");
        // There is no direct reader yet, but the insert must not crash and
        // must survive another open.
    }

    std::remove(db.c_str());
    return 0;
}
