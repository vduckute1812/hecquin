#include "voice/UtteranceRouter.hpp"

#include "ai/CommandProcessor.hpp"

#include <utility>

namespace hecquin::voice {

UtteranceRouter::UtteranceRouter(CommandProcessor& commands,
                                 const ListenerMode& mode,
                                 ModeAwareCallback drill_cb,
                                 ModeAwareCallback tutor_cb)
    : commands_(commands),
      mode_(mode),
      drill_cb_(std::move(drill_cb)),
      tutor_cb_(std::move(tutor_cb)) {}

UtteranceRouter::Result UtteranceRouter::route(const Utterance& utterance) const {
    Result r;

    // Always give the fast local matcher a chance first — this is what
    // lets the user switch modes by voice ("start english lesson" /
    // "exit lesson" / "start pronunciation drill" / "exit drill") from
    // any current mode.
    if (auto local = commands_.match_local(utterance.transcript)) {
        r.action = *local;
        r.from_local_intent = true;
        return r;
    }

    if (mode_ == ListenerMode::Drill && drill_cb_) {
        r.action = drill_cb_(utterance);
        return r;
    }
    if (mode_ == ListenerMode::Lesson && tutor_cb_) {
        r.action = tutor_cb_(utterance);
        return r;
    }
    // Fall back to the external API (handled by the full `process`
    // pipeline).
    r.action = commands_.process(utterance.transcript);
    return r;
}

} // namespace hecquin::voice
