#pragma once

#include "actions/Action.hpp"

#include <string>
#include <utility>

/**
 * Music intent helpers.
 *
 *   1. `prompt()`     — wake phrase ("open music") acknowledged; listener
 *                       enters `ListenerMode::Music` via
 *                       `VoiceListener::apply_local_intent_side_effects_`.
 *   2. `playback()`   — emitted by `MusicSession` once the song query is
 *                       resolved and playback has been dispatched to the
 *                       background thread.  Listener drops back into its
 *                       home mode immediately so subsequent commands ("stop
 *                       music", "pause music", …) are heard while the song
 *                       streams.  When the search returned no result the
 *                       factory emits `MusicNotFound` instead so the
 *                       speaker-bleed gate stays disengaged.
 *   3. `cancel()`     — "stop / cancel / exit / close music".  Side-effect
 *                       handler invokes `MusicSession::abort()`.
 *   4. `pause()`      — "pause music".  Best-effort suspend.
 *   5. `resume()`     — "continue / resume music".  Counterpart to pause.
 *
 * Each helper carries the original transcript so telemetry / pipeline
 * events can attribute the action back to the user's words.
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
        if (ok) {
            a.kind = ActionKind::MusicPlayback;
            a.reply = title.empty()
                ? std::string{"Now playing your request."}
                : std::string{"Now playing "} + title + ".";
        } else {
            a.kind = ActionKind::MusicNotFound;
            a.reply = "Sorry, I couldn't find that song.";
        }
        a.transcript = std::move(transcript);
        return a;
    }

    [[nodiscard]] static Action cancel(std::string transcript) {
        Action a;
        a.kind = ActionKind::MusicCancel;
        a.reply = "Okay, stopping music.";
        a.transcript = std::move(transcript);
        return a;
    }

    [[nodiscard]] static Action pause(std::string transcript) {
        Action a;
        a.kind = ActionKind::MusicPause;
        a.reply = "Paused.";
        a.transcript = std::move(transcript);
        return a;
    }

    [[nodiscard]] static Action resume(std::string transcript) {
        Action a;
        a.kind = ActionKind::MusicResume;
        a.reply = "Resuming.";
        a.transcript = std::move(transcript);
        return a;
    }
};
