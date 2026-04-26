#pragma once

/** High-level outcome of routing a transcript through the AI / command layer. */
enum class ActionKind {
    None,
    LocalDevice,
    InteractionTopicSearch,
    /**
     * Music conversation, multi-turn.  The split between
     * `MusicPlayback` (success) and `MusicNotFound` (search miss) lets
     * the listener's side-effect handler decide whether to engage the
     * speaker-bleed gate without sniffing the reply string:
     *
     *   1. `MusicSearchPrompt` — user said "open music"; assistant asks for a
     *      song name and the listener enters `ListenerMode::Music`.
     *   2. `MusicPlayback`     — the song query was resolved and playback
     *      is now streaming on the background worker thread.  Listener
     *      drops back to its home mode and tells the VAD collector to
     *      stop folding speaker bleed into the noise floor.
     *   3. `MusicNotFound`     — the provider's search returned nothing
     *      (or the query was empty).  Same mode-exit as `MusicPlayback`
     *      but without the noise-floor gate, since no audio is playing.
     *   4. `MusicCancel`       — user said "stop / cancel / exit / close
     *      music".  Side-effect handler aborts any in-flight playback.
     *   5. `MusicPause`        — user said "pause music".  Best-effort: the
     *      provider may pause the audio device; if unsupported the action is
     *      acknowledged but playback continues.
     *   6. `MusicResume`       — user said "continue / resume music".
     *      Counterpart to `MusicPause`.
     */
    MusicSearchPrompt,
    MusicPlayback,
    MusicNotFound,
    MusicCancel,
    MusicPause,
    MusicResume,
    ExternalApi,
    EnglishLesson,
    LessonModeToggle,
    PronunciationFeedback,
    DrillModeToggle,
};
