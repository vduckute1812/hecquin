// Regression for issue #5: a single-line JSON document larger than the chunk
// budget must be split into multiple prose chunks instead of being shoved
// into one giant chunk that trips the embedding API's per-input token cap.

#include "learning/ingest/JsonlChunker.hpp"

#include <iostream>
#include <string>

using hecquin::learning::ingest::JsonlChunker;

namespace {
int fail(const char* message) {
    std::cerr << "[test_jsonl_chunker_single_line_json] FAIL: " << message << std::endl;
    return 1;
}
} // namespace

int main() {
    constexpr int kBudget = 64;
    JsonlChunker chunker(kBudget);

    // 1. Single-line JSON > budget: must split into many small chunks.
    {
        const std::string single_line = "{\"data\":\"" + std::string(500, 'x') + "\"}";
        const auto out = chunker.chunk(single_line);
        if (out.size() < 2) return fail("single-line JSON > budget should split");
        for (const auto& c : out) {
            if (static_cast<int>(c.size()) > kBudget * 2) {
                return fail("each prose chunk should respect the budget");
            }
        }
    }

    // 2. Single-line JSON <= budget: pass through as one chunk.
    {
        const std::string short_line = R"({"id":1,"word":"hi"})";
        const auto out = chunker.chunk(short_line);
        if (out.size() != 1) return fail("short single-line JSON should be one chunk");
    }

    // 3. Real JSONL still chunks line-by-line.
    {
        const std::string doc =
            R"({"id":1})" "\n"
            R"({"id":2})" "\n"
            R"({"id":3})" "\n";
        const auto out = chunker.chunk(doc);
        if (out.empty()) return fail("jsonl input should produce chunks");
        for (const auto& c : out) {
            if (c.empty() || c.back() != '}') {
                return fail("jsonl chunk must preserve object boundary");
            }
        }
    }

    // 4. JSON with only a trailing newline counts as single-line too.
    {
        const std::string trailing_nl =
            "{\"k\":\"" + std::string(500, 'y') + "\"}\n";
        const auto out = chunker.chunk(trailing_nl);
        if (out.size() < 2) {
            return fail("single-line + trailing newline > budget should split");
        }
    }

    std::cout << "[test_jsonl_chunker_single_line_json] all assertions passed" << std::endl;
    return 0;
}
