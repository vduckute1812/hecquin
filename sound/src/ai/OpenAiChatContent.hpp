#pragma once

#include <optional>
#include <string>

/**
 * Best-effort read of the first `"content"` string after `"choices"` / `"message"` in a chat completion JSON body.
 * Handles common `\\` escapes and `\\uXXXX` (BMP only; paired UTF-16 surrogates are not merged).
 */
std::optional<std::string> extract_openai_chat_assistant_content(const std::string& json_body);
