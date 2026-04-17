#pragma once

#include "actions/Action.hpp"

#include <string>

/** User asked to open or search music (local intent before any player integration). */
struct MusicAction {
    [[nodiscard]] Action into_action(std::string transcript) const {
        Action a;
        a.kind = ActionKind::InteractionMusicSearch;
        a.reply = "Opening music search. What would you like to hear?";
        a.transcript = std::move(transcript);
        return a;
    }
};
