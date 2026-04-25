#pragma once

#include "actions/Action.hpp"
#include "voice/VoiceListener.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

class CommandProcessor;

namespace hecquin::voice {

/**
 * Chain of Responsibility for routing a collected utterance to the right
 * handler.  The chain the listener installs today is:
 *
 *   1. Local-intent matcher (`CommandProcessor::match_local`) — handles
 *      mode toggles ("start lesson", "exit drill") and other fast
 *      string-pattern actions before anything else sees the transcript.
 *   2. Drill callback (when the listener is in Drill mode).
 *   3. Tutor callback (when the listener is in Lesson mode).
 *   4. Fallback: `CommandProcessor::process` (the full LLM round-trip).
 *
 * Each handler returns an `Action`.  Returning `std::nullopt` from a
 * handler passes the utterance down the chain.  The router also reports
 * whether the resolved action came from the local matcher so the caller
 * can update mode state accordingly.
 */
class UtteranceRouter {
public:
    using ModeAwareCallback = std::function<Action(const Utterance&)>;
    /** Music callback — takes the raw transcript, returns a playback action. */
    using MusicCallback = std::function<Action(const std::string&)>;

    UtteranceRouter(CommandProcessor& commands,
                    const ListenerMode& mode,
                    ModeAwareCallback drill_cb,
                    ModeAwareCallback tutor_cb,
                    MusicCallback music_cb = {});

    struct Result {
        Action action;
        /** True iff the action came from `CommandProcessor::match_local`. */
        bool from_local_intent = false;
    };

    Result route(const Utterance& utterance) const;

private:
    CommandProcessor& commands_;
    const ListenerMode& mode_;
    ModeAwareCallback drill_cb_;
    ModeAwareCallback tutor_cb_;
    MusicCallback     music_cb_;
};

} // namespace hecquin::voice
