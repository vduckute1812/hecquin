#pragma once

#include <string>

namespace hecquin::ai {

/**
 * Map an HTTP status code to a short phrase that is safe to read aloud
 * through Piper.  The full response body is logged separately to stderr
 * for operators — we do not want to dump raw JSON through TTS.
 *
 * Shared between `ChatClient::ask` and
 * `EnglishTutorProcessor::call_llm_` so the buckets (auth / not-found /
 * timeout / busy / 5xx / other 4xx / generic) stay consistent across
 * the two call sites.
 */
std::string short_reply_for_status(int status);

} // namespace hecquin::ai
