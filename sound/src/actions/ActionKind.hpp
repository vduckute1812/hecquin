#pragma once

/** High-level outcome of routing a transcript through the AI / command layer. */
enum class ActionKind {
    None,
    LocalDevice,
    InteractionTopicSearch,
    /**
     * Music conversation, multi-turn.  See `MusicAction` factories for
     * the per-kind choreography.
     */
    MusicSearchPrompt,
    MusicPlayback,
    MusicNotFound,
    MusicCancel,
    MusicPause,
    MusicResume,
    /** "louder" / "turn it up" — provider best-effort gain bump. */
    MusicVolumeUp,
    /** "quieter" / "turn it down" — provider best-effort gain cut. */
    MusicVolumeDown,
    /** "skip" / "next song" — provider may treat as a stop + next pick. */
    MusicSkip,
    ExternalApi,
    EnglishLesson,
    LessonModeToggle,
    PronunciationFeedback,
    DrillModeToggle,
    /**
     * "Next / again / skip / continue" while in `ListenerMode::Drill`.
     * Used by the ready-gate (`HECQUIN_DRILL_AUTO_ADVANCE=0`) to let the
     * learner pace each sentence themselves instead of being immediately
     * pushed to the next attempt after feedback TTS.  Listener responds
     * by flushing `pending_drill_announce_` on the next loop iteration.
     */
    DrillAdvance,
    /**
     * "Stop / cancel / never mind / forget it" — universal abort.  Side
     * effects: fire the in-flight TTS abort fuse and drop any pending
     * follow-on (drill announce, cooldown filler).  Reply is a 0-byte
     * earcon so the user gets immediate audio acknowledgement without
     * a Piper round-trip.
     */
    AbortReply,
    /**
     * "Help / what can I do / commands" — speaks a mode-aware capability
     * summary.  Replies are loaded from `<prompts_dir>/help_<mode>.txt`
     * with a built-in fallback so the assistant always has something to
     * say even before any prompt files are authored.
     */
    Help,
    /**
     * "Go to sleep / mute yourself / stop listening" — flips listener
     * into `ListenerMode::Asleep`.  Only the wake intent (or a hardware
     * push-to-talk press) routes from there.
     */
    Sleep,
    /**
     * "Wake up / hello hecquin" — leaves `Asleep` mode back to the
     * binary's home mode.
     */
    Wake,
    /**
     * "I'm <name> / this is <name>" — namespace future progress writes
     * to a specific user row.  Reply confirms the switch.
     */
    IdentifyUser,
};
