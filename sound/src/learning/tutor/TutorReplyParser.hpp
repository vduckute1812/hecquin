#pragma once

#include "actions/GrammarCorrectionAction.hpp"

#include <string>

namespace hecquin::learning::tutor {

/**
 * Parse a free-form tutor reply into a structured
 * `GrammarCorrectionAction`.
 *
 * Recognised lines (case-insensitive, `:` or `-` separator):
 *   "You said: <original>"
 *   "Better:   <corrected>"
 *   "Reason:   <why>"
 *
 * If neither `Better:` nor `Reason:` matches, the entire raw reply is
 * dropped into `explanation` and `corrected` falls back to the user's
 * original text — that way the user always hears *something* useful.
 */
GrammarCorrectionAction parse_tutor_reply(const std::string& raw,
                                          const std::string& fallback_original);

} // namespace hecquin::learning::tutor
