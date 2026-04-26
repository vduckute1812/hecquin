#pragma once

#include <string>

namespace hecquin::learning {
class RetrievalService;
}

namespace hecquin::learning::tutor {

/**
 * Thin wrapper around `RetrievalService::build_context` that captures
 * the tutor-side RAG knobs (top_k, max char budget) so the processor
 * does not have to thread them through to every call site.
 *
 * Pure with respect to its inputs (modulo whatever side effects the
 * underlying retrieval service performs); replaceable behind tests.
 */
class TutorContextBuilder {
public:
    TutorContextBuilder(RetrievalService& retrieval,
                        int rag_top_k,
                        int rag_max_context_chars);

    /** Build the bullet-list context block, capped at `max_chars`. */
    std::string build(const std::string& query) const;

private:
    RetrievalService& retrieval_;
    int top_k_;
    int max_chars_;
};

} // namespace hecquin::learning::tutor
