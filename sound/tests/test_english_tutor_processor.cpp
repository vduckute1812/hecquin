// Integration test for EnglishTutorProcessor: canned chat response through a
// FakeHttp client, real LearningStore + EmbeddingClient (itself backed by
// FakeHttp) for RAG, and assertions on (a) parsed GrammarCorrectionAction
// output and (b) RAG context truncation at `rag_max_context_chars`.
//
// No network: both HTTP seams accept injected IHttpClient instances.  SQLite
// is on by default in this build so we can stand up a temporary store in
// /tmp without polluting the real one.

#include "ai/IHttpClient.hpp"
#include "config/ai/AiClientConfig.hpp"
#include "learning/EmbeddingClient.hpp"
#include "learning/EnglishTutorProcessor.hpp"
#include "learning/ProgressTracker.hpp"
#include "learning/RetrievalService.hpp"
#include "learning/store/LearningStore.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

int fail(const char* msg) {
    std::cerr << "[test_english_tutor_processor] FAIL: " << msg << std::endl;
    return 1;
}

std::string make_tmp_db() {
    char buf[] = "/tmp/hecquin_tutor_XXXXXX";
    const int fd = mkstemp(buf);
    if (fd < 0) return {};
    close(fd);
    std::remove(buf);
    return std::string(buf) + ".sqlite";
}

// Returns the same canned payload for every call.  Used as the backing
// transport for both the embedding client and the chat client.
class StaticHttp final : public hecquin::ai::IHttpClient {
public:
    HttpResult canned{200, ""};
    std::string last_body;
    int calls = 0;

    std::optional<HttpResult> post_json(const std::string& /*url*/,
                                        const std::string& /*bearer*/,
                                        const std::string& json_body,
                                        long /*timeout*/) override {
        last_body = json_body;
        ++calls;
        return canned;
    }
};

} // namespace

int main() {
    using namespace hecquin::learning;

    constexpr int kDim = 4;

    // ------------------------------------------------------------------
    // Build a tmp LearningStore with two documents containing long bodies
    // so we can observe RAG-context truncation.
    // ------------------------------------------------------------------
    const std::string db_path = make_tmp_db();
    if (db_path.empty()) return fail("cannot allocate tmp db path");

    LearningStore store(db_path, kDim);
    if (!store.open()) {
        std::remove(db_path.c_str());
        return fail("store open() failed");
    }

    auto make_long_body = [](char c, std::size_t n) {
        return std::string(n, c);
    };

    const std::vector<float> vec_a{1.0f, 0.0f, 0.0f, 0.0f};
    const std::vector<float> vec_b{0.0f, 1.0f, 0.0f, 0.0f};

    {
        DocumentRecord d;
        d.source = "tmp"; d.kind = "custom"; d.title = "Alpha";
        d.body = make_long_body('a', 5000);
        if (!store.upsert_document(d, vec_a))
            return fail("upsert alpha");
    }
    {
        DocumentRecord d;
        d.source = "tmp"; d.kind = "custom"; d.title = "Beta";
        d.body = make_long_body('b', 5000);
        if (!store.upsert_document(d, vec_b))
            return fail("upsert beta");
    }

    // ------------------------------------------------------------------
    // Embedding client backed by a fake HTTP — it always returns the same
    // unit vector so `query_top_k` finds our docs deterministically.
    // ------------------------------------------------------------------
    StaticHttp embed_http;
    embed_http.canned = {200, R"({"data":[{"embedding":[1.0,0.0,0.0,0.0]}]})"};

    AiClientConfig embed_cfg;
    embed_cfg.api_key = "fake";
    embed_cfg.embeddings_url = "https://unit.test/embeddings";
    embed_cfg.embedding_model = "fake-embed";
    embed_cfg.embedding_dim = kDim;
    EmbeddingClient embed(embed_cfg, embed_http);

    RetrievalService retrieval(store, embed);
    ProgressTracker progress(store, "lesson");

    // ------------------------------------------------------------------
    // Fake chat response — one canonical tutor reply.  The processor
    // parses "You said / Better / Reason".
    // ------------------------------------------------------------------
    StaticHttp chat_http;
    chat_http.canned = {200,
        R"({"choices":[{"message":{"role":"assistant","content":)"
        R"("You said: i has book. Better: I have a book. Reason: use have with I."}}]})"};

    AiClientConfig chat_cfg;
    chat_cfg.api_key = "fake";
    chat_cfg.chat_completions_url = "https://unit.test/chat";
    chat_cfg.model = "fake-chat";
    chat_cfg.tutor_system_prompt = "You are a tutor.";

    // Small rag_max_context_chars so we can detect truncation.
    EnglishTutorConfig tcfg;
    tcfg.rag_top_k = 2;
    tcfg.rag_max_context_chars = 200;
    EnglishTutorProcessor tutor(chat_cfg, retrieval, progress, tcfg);
    tutor.set_http_client_for_test(&chat_http);

    // ------------------------------------------------------------------
    // 1. Happy path: tutor correctly parses the canned reply.
    // ------------------------------------------------------------------
    {
        const Action a = tutor.process("i has book");
        if (a.kind != ActionKind::EnglishLesson)
            return fail("kind should be EnglishLesson");
        if (a.reply.find("I have a book") == std::string::npos)
            return fail("reply should surface the 'Better:' correction");
        if (a.reply.find("use have with I") == std::string::npos)
            return fail("reply should surface the 'Reason:' explanation");
    }

    // ------------------------------------------------------------------
    // 2. RAG-context truncation: the chat body built by the processor
    //    must not include the full 5 kB documents — we capped at 200.
    //    Concretely the JSON body should weigh in at << 5 kB of repeated
    //    'a' / 'b' characters, and the sum of the 'a' run and 'b' run
    //    must respect the cap.
    // ------------------------------------------------------------------
    {
        const std::string& body = chat_http.last_body;
        if (body.empty()) return fail("chat body was not captured");

        const std::size_t a_run = body.find("aaaaaaaaaa");
        const std::size_t b_run = body.find("bbbbbbbbbb");
        if (a_run == std::string::npos && b_run == std::string::npos)
            return fail("expected at least one RAG snippet in body");

        // Count the 'a' characters in the body — well below the 5 k we
        // stored, and below the 200-char cap plus JSON scaffolding.
        std::size_t a_count = 0, b_count = 0;
        for (char c : body) {
            if (c == 'a') ++a_count;
            if (c == 'b') ++b_count;
        }
        if (a_count + b_count > 300)  // cap (200) + a little JSON slack
            return fail("RAG context exceeded rag_max_context_chars cap");
        if (a_count + b_count < 50)
            return fail("RAG context looks suspiciously empty — "
                        "retrieval may have returned nothing");
    }

    // ------------------------------------------------------------------
    // 3. Transport-level failure: processor returns a graceful fallback
    //    instead of crashing.  nullopt from the client simulates a
    //    timeout or DNS failure.
    // ------------------------------------------------------------------
    {
        class DropHttp final : public hecquin::ai::IHttpClient {
        public:
            std::optional<HttpResult> post_json(const std::string&, const std::string&,
                                                const std::string&, long) override {
                return std::nullopt;
            }
        } drop;
        EnglishTutorProcessor t2(chat_cfg, retrieval, progress, tcfg);
        t2.set_http_client_for_test(&drop);
        const Action a = t2.process("test");
        if (a.kind != ActionKind::EnglishLesson)
            return fail("transport failure should still emit EnglishLesson");
        if (a.reply.find("could not reach") == std::string::npos)
            return fail("transport failure reply missing 'could not reach'");
    }

    // ------------------------------------------------------------------
    // 4. Non-2xx response: return a short, spoken-safe bucket phrase
    //    (shared with ChatClient via ai::HttpReplyBuckets).
    // ------------------------------------------------------------------
    {
        StaticHttp err;
        err.canned = {500, R"({"error":"boom"})"};
        EnglishTutorProcessor t3(chat_cfg, retrieval, progress, tcfg);
        t3.set_http_client_for_test(&err);
        const Action a = t3.process("test");
        if (a.reply.find("temporarily unavailable") == std::string::npos)
            return fail("5xx reply should route through short_reply_for_status");
    }

    // ------------------------------------------------------------------
    // 5. Unparseable reply: processor must not crash.
    // ------------------------------------------------------------------
    {
        StaticHttp junk;
        junk.canned = {200, R"({"oops": true})"};
        EnglishTutorProcessor t4(chat_cfg, retrieval, progress, tcfg);
        t4.set_http_client_for_test(&junk);
        const Action a = t4.process("test");
        if (a.reply.find("could not be parsed") == std::string::npos)
            return fail("unparseable reply should surface a parse error");
    }

    progress.close();
    std::remove(db_path.c_str());

    std::cout << "[test_english_tutor_processor] OK" << std::endl;
    return 0;
}
