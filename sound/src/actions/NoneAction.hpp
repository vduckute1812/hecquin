#pragma once

#include "actions/Action.hpp"

/** Factory helpers for non-routed or degenerate outcomes. */
struct NoneAction {
    [[nodiscard]] static Action empty_transcript() {
        Action a;
        a.kind = ActionKind::None;
        a.reply = "(empty transcript)";
        a.transcript.clear();
        return a;
    }
};
