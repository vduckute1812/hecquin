#pragma once

#include "config/ai/AiClientConfig.hpp"

#include <string>

namespace hecquin::learning::tutor {

/**
 * Build the OpenAI-chat-compatible JSON request body sent to the
 * tutor model.  Pure / stateless — exposed as a free function so it
 * can be unit-tested without standing up a full processor.
 *
 * `context` may be empty; when non-empty it is appended to the system
 * prompt under a "Reference snippets" header.  Non-UTF-8 bytes coming
 * out of ingested corpora are replaced with U+FFFD so the dumper
 * cannot abort mid-turn.
 */
std::string build_chat_body(const AiClientConfig& ai,
                            const std::string& user_text,
                            const std::string& context);

} // namespace hecquin::learning::tutor
