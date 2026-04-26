#include "learning/tutor/TutorContextBuilder.hpp"

#include "learning/RetrievalService.hpp"

namespace hecquin::learning::tutor {

TutorContextBuilder::TutorContextBuilder(RetrievalService& retrieval,
                                         int rag_top_k,
                                         int rag_max_context_chars)
    : retrieval_(retrieval),
      top_k_(rag_top_k),
      max_chars_(rag_max_context_chars) {}

std::string TutorContextBuilder::build(const std::string& query) const {
    return retrieval_.build_context(query, top_k_, max_chars_);
}

} // namespace hecquin::learning::tutor
