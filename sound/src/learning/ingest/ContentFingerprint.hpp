#pragma once

#include <string>

namespace hecquin::learning::ingest {

/**
 * FNV-1a 64-bit fingerprint over (size + content).  Deterministic across
 * runs and platforms; used by the ingestor to skip unchanged files on
 * re-runs.  The returned string is `<hex_size>-<hex_hash>`.
 */
std::string content_fingerprint(const std::string& content);

} // namespace hecquin::learning::ingest
