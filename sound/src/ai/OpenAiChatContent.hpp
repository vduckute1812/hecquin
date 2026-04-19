#pragma once

#include <optional>
#include <string>

/**
 * Extract the assistant's text from an OpenAI-compatible chat completion body
 * (`choices[0].message.content`).  Returns `nullopt` on parse error, missing
 * fields, or non-string content.  Implemented with a real JSON parser so
 * nested `"content"` keys (e.g. tool-call messages) do not fool the scan.
 */
std::optional<std::string> extract_openai_chat_assistant_content(const std::string& json_body);
