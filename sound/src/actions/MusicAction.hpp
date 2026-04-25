#pragma once

#include "actions/Action.hpp"

#include <string>
#include <utility>

/**
 * Music intent helpers.  "Open music" is a two-turn conversation:
 *
 *   1. `prompt()`            — acknowledge the wake phrase and ask the user
 *                              for a song name.  Puts the listener into
 *                              `ListenerMode::Music` via
 *                              `VoiceListener::apply_local_intent_side_effects_`.
 *   2. `playback()`          — emitted by `MusicSession` after the provider
 *                              has searched + played (or failed) the user's
 *                              query.  Drops the listener back into its home
 *                              mode.
 *
 * `cancel()` covers the user bailing out mid-flow ("cancel music" / "stop
 * music") before providing a song name; it reuses the `MusicPlayback` kind
 * so the mode-exit side effect is identical.
 */
struct MusicAction {
    [[nodiscard]] static Action prompt(std::string transcript) {
        Action a;
        a.kind = ActionKind::MusicSearchPrompt;
        a.reply = "What music would you like to play?";
        a.transcript = std::move(transcript);
        return a;
    }

    [[nodiscard]] static Action playback(std::string transcript,
                                         bool ok,
                                         const std::string& title) {
        Action a;
        a.kind = ActionKind::MusicPlayback;
        if (ok) {
            a.reply = title.empty()
                ? std::string{"Now playing your request."}
                : std::string{"Now playing "} + title + ".";
        } else {
            a.reply = "Sorry, I couldn't find that song.";
        }
        a.transcript = std::move(transcript);
        return a;
    }

    [[nodiscard]] static Action cancel(std::string transcript) {
        Action a;
        a.kind = ActionKind::MusicPlayback;
        a.reply = "Okay, cancelling music.";
        a.transcript = std::move(transcript);
        return a;
    }
};
