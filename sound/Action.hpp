#pragma once

#include <string>

/** High-level outcome of routing a transcript through the AI / command layer. */
enum class ActionKind {
    None,
    LocalDevice,
    InteractionTopicSearch,
    InteractionMusicSearch,
    ExternalApi,
    AssistantSdk,
};

struct Action {
    ActionKind kind = ActionKind::None;
    /** Short text suitable for TTS or UI (command ack, API summary, or error). */
    std::string reply;
    /** Original or normalized user text that triggered this action. */
    std::string transcript;
};
