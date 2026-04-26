#pragma once

/**
 * Listener-loop mode tag.  Lifted to its own header so subsystems that
 * only need the enum (`ActionSideEffectRegistry`, future telemetry
 * hooks) don't have to drag in all of `VoiceListener.hpp`'s
 * dependencies (Whisper, Audio capture, Piper paths, …).
 *
 * Lives in the global namespace because that's where it has always
 * been — moving it would force every downstream caller to re-qualify
 * a name that is already widely used unqualified.
 */
enum class ListenerMode {
    Assistant,
    Lesson,
    Drill,
    /**
     * Entered when the local matcher resolves "open music".  The next
     * utterance is treated as a song query (routed to `music_cb_`) and
     * the listener returns to `home_mode_` after the resulting
     * `MusicPlayback` action is emitted — mirroring the short-lived
     * lifecycle of `Drill`.
     */
    Music,
    /**
     * Sleep mode: the pipeline still captures audio but only the wake
     * intent (or a configured hardware push-to-talk press) routes a
     * transcript downstream.  Entered via `ActionKind::Sleep`; left via
     * `ActionKind::Wake`.
     */
    Asleep,
};
