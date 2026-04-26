#include "voice/WhisperPostFilter.hpp"

#include <cassert>
#include <iostream>
#include <string>

using hecquin::voice::WhisperPostFilter;

namespace {

void test_passes_clean_transcript() {
    WhisperConfig cfg;
    auto out = WhisperPostFilter::filter("Hello world.", 0.1f, cfg);
    assert(out.has_value());
    assert(*out == "Hello world.");
}

void test_strips_bracketed_annotations() {
    WhisperConfig cfg;
    auto out = WhisperPostFilter::filter(
        "[MUSIC]   ", 0.1f, cfg);
    // After stripping [MUSIC] and trimming, the content is empty
    // → rejected.  When stripped == joined we suppress the warning,
    // here they differ so the warning will print but we only assert
    // the gate.
    assert(!out.has_value());
}

void test_strips_parenthetical_annotations() {
    WhisperConfig cfg;
    auto out = WhisperPostFilter::filter(
        "(applause) (laughter)", 0.1f, cfg);
    assert(!out.has_value());
}

void test_keeps_speech_around_annotations() {
    WhisperConfig cfg;
    auto out = WhisperPostFilter::filter(
        "[MUSIC] turn it down please", 0.1f, cfg);
    assert(out.has_value());
    assert(out->find("turn it down please") != std::string::npos);
}

void test_min_alnum_gate() {
    WhisperConfig cfg;
    cfg.min_alnum_chars = 5;
    auto out = WhisperPostFilter::filter("hi.", 0.1f, cfg);
    assert(!out.has_value());
}

void test_min_alnum_passes_at_threshold() {
    WhisperConfig cfg;
    cfg.min_alnum_chars = 5;
    auto out = WhisperPostFilter::filter("hello!", 0.1f, cfg);
    assert(out.has_value());
}

void test_no_speech_prob_gate() {
    WhisperConfig cfg;
    cfg.no_speech_prob_max = 0.5f;
    auto out = WhisperPostFilter::filter("real speech here", 0.9f, cfg);
    assert(!out.has_value());
}

void test_no_speech_prob_at_threshold_passes() {
    WhisperConfig cfg;
    cfg.no_speech_prob_max = 0.5f;
    auto out = WhisperPostFilter::filter("real speech here", 0.5f, cfg);
    assert(out.has_value());
}

void test_trims_leading_trailing_whitespace() {
    WhisperConfig cfg;
    auto out = WhisperPostFilter::filter(
        "   hello world   ", 0.1f, cfg);
    assert(out.has_value());
    assert(*out == "hello world");
}

} // namespace

int main() {
    test_passes_clean_transcript();
    test_strips_bracketed_annotations();
    test_strips_parenthetical_annotations();
    test_keeps_speech_around_annotations();
    test_min_alnum_gate();
    test_min_alnum_passes_at_threshold();
    test_no_speech_prob_gate();
    test_no_speech_prob_at_threshold_passes();
    test_trims_leading_trailing_whitespace();
    std::cout << "[whisper_post_filter] all tests passed" << std::endl;
    return 0;
}
