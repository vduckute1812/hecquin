#pragma once

#include "actions/ActionKind.hpp"

#include <string>

struct Action {
    ActionKind kind = ActionKind::None;
    /** Short text suitable for TTS or UI (command ack, API summary, or error). */
    std::string reply;
    /** Original or normalized user text that triggered this action. */
    std::string transcript;
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
        case ActionKind::InteractionMusicSearch:
            return "MusicSearch";
        case ActionKind::ExternalApi:
            return "ExternalApi";
        case ActionKind::AssistantSdk:
            return "AssistantSdk";
        case ActionKind::EnglishLesson:
            return "EnglishLesson";
        case ActionKind::LessonModeToggle:
            return "LessonModeToggle";
        case ActionKind::PronunciationFeedback:
            return "PronunciationFeedback";
        case ActionKind::DrillModeToggle:
            return "DrillModeToggle";
    }
    return "Unknown";
}
