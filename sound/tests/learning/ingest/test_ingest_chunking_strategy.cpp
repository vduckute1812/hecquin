#include "learning/ingest/ChunkingStrategy.hpp"
#include "learning/ingest/JsonlChunker.hpp"
#include "learning/ingest/ProseChunker.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using hecquin::learning::ingest::IChunker;
using hecquin::learning::ingest::JsonlChunker;
using hecquin::learning::ingest::make_chunker_for_extension;
using hecquin::learning::ingest::ProseChunker;

namespace {

int failures = 0;

void expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "[FAIL] " << label << std::endl;
        ++failures;
    }
}

} // namespace

int main() {
    // 1. Factory picks JsonlChunker for jsonl + json.
    {
        auto c_jsonl = make_chunker_for_extension("jsonl", 1800, 200);
        auto c_json  = make_chunker_for_extension("json",  1800, 200);
        expect(dynamic_cast<JsonlChunker*>(c_jsonl.get()) != nullptr,
               "jsonl ext dispatches to JsonlChunker");
        expect(dynamic_cast<JsonlChunker*>(c_json.get()) != nullptr,
               "json ext dispatches to JsonlChunker");
    }

    // 2. Factory falls back to ProseChunker for prose/unknown extensions.
    {
        auto c_md  = make_chunker_for_extension("md",  1800, 200);
        auto c_txt = make_chunker_for_extension("txt", 1800, 200);
        auto c_any = make_chunker_for_extension("",    1800, 200);
        expect(dynamic_cast<ProseChunker*>(c_md.get())  != nullptr,
               "md ext dispatches to ProseChunker");
        expect(dynamic_cast<ProseChunker*>(c_txt.get()) != nullptr,
               "txt ext dispatches to ProseChunker");
        expect(dynamic_cast<ProseChunker*>(c_any.get()) != nullptr,
               "unknown ext falls back to ProseChunker");
    }

    // 3. JsonlChunker preserves whole lines (never splits a JSON object).
    {
        JsonlChunker chunker(64);
        const std::string doc =
            R"({"id":1,"word":"hello"})" "\n"
            R"({"id":2,"word":"world"})" "\n"
            R"({"id":3,"word":"again"})" "\n";
        auto out = chunker.chunk(doc);
        expect(!out.empty(), "jsonl chunker produces at least one chunk");
        // Every chunk must end cleanly on `}` — no mid-object slices.
        for (const auto& c : out) {
            expect(!c.empty() && c.back() == '}',
                   "jsonl chunk preserves object boundary");
        }
    }

    // 4. ProseChunker honours budget on plain text.
    {
        ProseChunker chunker(40, 8);
        const std::string doc(200, 'x');
        auto out = chunker.chunk(doc);
        expect(out.size() >= 2, "prose chunker splits long input");
    }

    if (failures == 0) {
        std::cout << "[test_ingest_chunking_strategy] all assertions passed" << std::endl;
        return 0;
    }
    return 1;
}
