#pragma once

#include "actions/Action.hpp"

#include <sstream>
#include <string>
#include <utility>

/**
 * Output of the English-tutor pipeline for a single user utterance.
 *
 * `original`    — the raw transcript we received.
 * `corrected`   — a grammar-fixed rewrite (may equal `original` when nothing is wrong).
 * `explanation` — short, spoken-friendly reason sentence.
 */
struct GrammarCorrectionAction {
    std::string original;
    std::string corrected;
    std::string explanation;

    /** Build a user-facing reply suitable for TTS. */
    [[nodiscard]] std::string to_reply() const {
        std::ostringstream oss;
        if (!original.empty()) {
            oss << "You said: " << original << ". ";
        }
        if (!corrected.empty() && corrected != original) {
            oss << "Better: " << corrected << ". ";
        } else {
            oss << "That sounded correct. ";
        }
        if (!explanation.empty()) {
            oss << explanation;
        }
        return oss.str();
    }

    [[nodiscard]] Action into_action(std::string transcript) const {
        Action a;
        a.kind = ActionKind::EnglishLesson;
        a.reply = to_reply();
        a.transcript = std::move(transcript);
        return a;
    }
};

/** Toggling action emitted by the command matcher to switch the bot into / out of lesson mode. */
struct LessonModeToggleAction {
    bool enable = true;
    std::string reply;

    [[nodiscard]] Action into_action(std::string transcript) const {
        Action a;
        a.kind = ActionKind::LessonModeToggle;
        a.reply = reply.empty() ? std::string(enable ? "English lesson mode on."
                                                     : "Lesson mode off.")
                                 : reply;
        a.transcript = std::move(transcript);
        a.enable = enable;
        return a;
    }
};
