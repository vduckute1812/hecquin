#pragma once

#include "actions/Action.hpp"

#include <optional>
#include <string>

namespace hecquin::ai {

/**
 * Pure regex-based intent matcher for the fast-path (no network, no I/O).
 *
 * Handles a small set of wake-word style commands: turn on/off the air
 * conditioner, open music, tell a story, and enter/leave lesson mode.
 * Trimming and lower-casing are done internally so callers can pass raw
 * transcripts.  Returns `nullopt` when nothing matches.
 */
class LocalIntentMatcher {
public:
    std::optional<Action> match(const std::string& transcript) const;
};

} // namespace hecquin::ai
