#pragma once

#include "actions/Action.hpp"

#include <string>
#include <utility>

/**
 * Factories for "system" intents — pipeline-wide behaviour toggles that
 * are not tied to music / lesson / drill domains.
 *
 *   - `help()`       : capability summary; reply text is mode-aware and
 *                      filled in by `LocalIntentMatcher` from a prompt
 *                      template (or the built-in default).
 *   - `abort()`      : universal "stop / never mind".  Empty reply so
 *                      `TtsResponsePlayer::speak` skips Piper — the
 *                      side-effect handler plays a short earcon and
 *                      drops any pending follow-on.
 *   - `sleep()`      : "go to sleep / mute yourself" — listener flips
 *                      to `ListenerMode::Asleep` until a wake intent
 *                      (or hardware PTT press) arrives.
 *   - `wake()`       : counterpart to `sleep()`.
 *   - `identify(name)`: "I'm Liam / this is Mia" — namespace future
 *                      progress writes to the named user.
 */
struct SystemAction {
    [[nodiscard]] static Action help(std::string reply,
                                     std::string transcript) {
        Action a;
        a.kind = ActionKind::Help;
        a.reply = std::move(reply);
        a.transcript = std::move(transcript);
        return a;
    }

    [[nodiscard]] static Action abort(std::string transcript) {
        Action a;
        a.kind = ActionKind::AbortReply;
        a.reply.clear();
        a.transcript = std::move(transcript);
        return a;
    }

    [[nodiscard]] static Action sleep(std::string transcript) {
        Action a;
        a.kind = ActionKind::Sleep;
        a.reply = "Going to sleep. Say wake up when you need me.";
        a.transcript = std::move(transcript);
        return a;
    }

    [[nodiscard]] static Action wake(std::string transcript) {
        Action a;
        a.kind = ActionKind::Wake;
        a.reply = "I'm here.";
        a.transcript = std::move(transcript);
        return a;
    }

    [[nodiscard]] static Action identify(std::string display_name,
                                         std::string transcript) {
        Action a;
        a.kind = ActionKind::IdentifyUser;
        if (display_name.empty()) {
            a.reply = "Got it.";
        } else {
            a.reply = std::string{"Hi "} + display_name + ".";
        }
        a.transcript = std::move(transcript);
        a.param = std::move(display_name);
        return a;
    }
};
