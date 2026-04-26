#include "learning/tutor/TutorReplyParser.hpp"

#include <cassert>
#include <iostream>
#include <string>

using hecquin::learning::tutor::parse_tutor_reply;

namespace {

void test_parses_canonical_format() {
    const std::string raw =
        "You said: i has book.\n"
        "Better: I have a book.\n"
        "Reason: use have with I.\n";
    auto out = parse_tutor_reply(raw, "i has book");
    assert(out.original == "i has book.");
    assert(out.corrected == "I have a book.");
    assert(out.explanation == "use have with I.");
}

void test_case_insensitive_labels() {
    const std::string raw =
        "YOU SAID: hello\n"
        "BETTER: Hello!\n"
        "REASON: capitalisation\n";
    auto out = parse_tutor_reply(raw, "hello");
    assert(out.original == "hello");
    assert(out.corrected == "Hello!");
    assert(out.explanation == "capitalisation");
}

void test_dash_separator() {
    const std::string raw =
        "You said - good morning\n"
        "Better - Good morning.\n"
        "Reason - punctuation\n";
    auto out = parse_tutor_reply(raw, "good morning");
    assert(out.corrected == "Good morning.");
    assert(out.explanation == "punctuation");
}

void test_only_better_field() {
    const std::string raw = "Better: corrected only\n";
    auto out = parse_tutor_reply(raw, "fallback");
    assert(out.original == "fallback");
    assert(out.corrected == "corrected only");
}

void test_unparseable_falls_back_to_full_text() {
    const std::string raw = "Some completely free-form reply.";
    auto out = parse_tutor_reply(raw, "user input");
    assert(out.original == "user input");
    // No "Better" → corrected falls back to original.
    assert(out.corrected == "user input");
    // No "Reason" + no "Better" → entire raw text becomes explanation.
    assert(out.explanation == "Some completely free-form reply.");
}

void test_trims_field_whitespace() {
    const std::string raw =
        "You said:    spaces    \n"
        "Better:    trimmed    \n";
    auto out = parse_tutor_reply(raw, "x");
    assert(out.original == "spaces");
    assert(out.corrected == "trimmed");
}

void test_uses_fallback_when_you_said_missing() {
    const std::string raw = "Better: fixed\nReason: because";
    auto out = parse_tutor_reply(raw, "user-original");
    assert(out.original == "user-original");
    assert(out.corrected == "fixed");
    assert(out.explanation == "because");
}

} // namespace

int main() {
    test_parses_canonical_format();
    test_case_insensitive_labels();
    test_dash_separator();
    test_only_better_field();
    test_unparseable_falls_back_to_full_text();
    test_trims_field_whitespace();
    test_uses_fallback_when_you_said_missing();
    std::cout << "[tutor_reply_parser] all tests passed" << std::endl;
    return 0;
}
