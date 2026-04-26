#pragma once

#include "actions/ActionKind.hpp"

#include <string>

struct Action {
    ActionKind kind = ActionKind::None;
    /** Short text suitable for TTS or UI (command ack, API summary, or error). */
    std::string reply;
    /** Original or normalized user text that triggered this action. */
    std::string transcript;
    /**
     * Meaningful only for `LessonModeToggle` / `DrillModeToggle` actions:
     * `true` means enter the mode, `false` means exit it.  Filled in by
     * `LocalIntentMatcher` so downstream consumers don't have to re-run the
     * regex on the transcript to figure out which direction the toggle went.
     */
    bool enable = false;
    /**
     * Free-form action parameter.  Currently only `IdentifyUser` populates
     * this with the captured display name (e.g. "mia") so the listener can
     * forward it to a `LearningStore::upsert_user` callback without
     * re-running the regex.  Other action kinds leave it empty.
     */
    std::string param;
};

/** Short ASCII label for logs (not localized). */
[[nodiscard]] inline constexpr const char* actionKindLabel(ActionKind k) noexcept {
    switch (k) {
        case ActionKind::None:
            return "None";
        case ActionKind::LocalDevice:
            return "LocalDevice";
        case ActionKind::InteractionTopicSearch:
            return "TopicSearch";
        case ActionKind::MusicSearchPrompt:
            return "MusicSearchPrompt";
        case ActionKind::MusicPlayback:
            return "MusicPlayback";
        case ActionKind::MusicNotFound:
            return "MusicNotFound";
        case ActionKind::MusicCancel:
            return "MusicCancel";
        case ActionKind::MusicPause:
            return "MusicPause";
        case ActionKind::MusicResume:
            return "MusicResume";
        case ActionKind::ExternalApi:
            return "ExternalApi";
        case ActionKind::EnglishLesson:
            return "EnglishLesson";
        case ActionKind::LessonModeToggle:
            return "LessonModeToggle";
        case ActionKind::PronunciationFeedback:
            return "PronunciationFeedback";
        case ActionKind::DrillModeToggle:
            return "DrillModeToggle";
        case ActionKind::DrillAdvance:
            return "DrillAdvance";
        case ActionKind::MusicVolumeUp:
            return "MusicVolumeUp";
        case ActionKind::MusicVolumeDown:
            return "MusicVolumeDown";
        case ActionKind::MusicSkip:
            return "MusicSkip";
        case ActionKind::AbortReply:
            return "AbortReply";
        case ActionKind::Help:
            return "Help";
        case ActionKind::Sleep:
            return "Sleep";
        case ActionKind::Wake:
            return "Wake";
        case ActionKind::IdentifyUser:
            return "IdentifyUser";
    }
    return "Unknown";
}
