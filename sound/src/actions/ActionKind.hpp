#pragma once

/** High-level outcome of routing a transcript through the AI / command layer. */
enum class ActionKind {
    None,
    LocalDevice,
    InteractionTopicSearch,
    InteractionMusicSearch,
    ExternalApi,
    EnglishLesson,
    LessonModeToggle,
    PronunciationFeedback,
    DrillModeToggle,
};
