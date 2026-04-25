#pragma once

/** High-level outcome of routing a transcript through the AI / command layer. */
enum class ActionKind {
    None,
    LocalDevice,
    InteractionTopicSearch,
    /**
     * Music conversation is a two-turn flow:
     *   1. `MusicSearchPrompt`  — user said "open music"; assistant asks for a
     *      song name and the listener enters `ListenerMode::Music`.
     *   2. `MusicPlayback`      — the user's song query has been searched
     *      + played (or failed / cancelled) and the listener returns to its
     *      home mode.
     */
    MusicSearchPrompt,
    MusicPlayback,
    ExternalApi,
    EnglishLesson,
    LessonModeToggle,
    PronunciationFeedback,
    DrillModeToggle,
};
