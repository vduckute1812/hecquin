#include "voice/ActionSideEffectRegistry.hpp"

namespace hecquin::voice {

namespace {

const ActionSideEffectDescriptor kNoop{};

// Order does not matter — the `switch` is a flat lookup, not a state
// machine.  Adding a new intent → add a `case`, fill in the row.  Most
// future entries (e.g. AudiobookPlayback / RadioCancel) will mirror an
// existing music row.
const ActionSideEffectDescriptor& lookup(ActionKind kind) {
    using MC = ActionSideEffectDescriptor::ModeChange;

    switch (kind) {
        case ActionKind::LessonModeToggle: {
            static const ActionSideEffectDescriptor d{
                MC::EnterIfEnable, ListenerMode::Lesson, nullptr, false};
            return d;
        }
        case ActionKind::DrillModeToggle: {
            static const ActionSideEffectDescriptor d{
                MC::EnterIfEnable, ListenerMode::Drill, nullptr, true};
            return d;
        }
        case ActionKind::MusicSearchPrompt: {
            static const ActionSideEffectDescriptor d{
                MC::Enter, ListenerMode::Music, nullptr, false};
            return d;
        }
        case ActionKind::MusicPlayback: {
            static const ActionSideEffectDescriptor d{
                MC::ExitTo, ListenerMode::Music,
                &MusicSideEffects::on_playback_started, false};
            return d;
        }
        case ActionKind::MusicNotFound: {
            static const ActionSideEffectDescriptor d{
                MC::ExitTo, ListenerMode::Music,
                &MusicSideEffects::on_playback_not_found, false};
            return d;
        }
        case ActionKind::MusicCancel: {
            // Order is intentional: mode_ is just an atomic enum write
            // (no observers), so dispatching the music hook second is
            // equivalent to the previous "abort first then exit mode"
            // ordering — the ~ms between them is unobservable to the
            // listener loop.
            static const ActionSideEffectDescriptor d{
                MC::ExitTo, ListenerMode::Music,
                &MusicSideEffects::on_cancel, false};
            return d;
        }
        case ActionKind::MusicPause: {
            static const ActionSideEffectDescriptor d{
                MC::None, ListenerMode::Assistant,
                &MusicSideEffects::on_pause, false};
            return d;
        }
        case ActionKind::MusicResume: {
            static const ActionSideEffectDescriptor d{
                MC::None, ListenerMode::Assistant,
                &MusicSideEffects::on_resume, false};
            return d;
        }
        case ActionKind::MusicVolumeUp: {
            static const ActionSideEffectDescriptor d{
                MC::None, ListenerMode::Assistant,
                &MusicSideEffects::on_volume_up, false};
            return d;
        }
        case ActionKind::MusicVolumeDown: {
            static const ActionSideEffectDescriptor d{
                MC::None, ListenerMode::Assistant,
                &MusicSideEffects::on_volume_down, false};
            return d;
        }
        case ActionKind::MusicSkip: {
            static const ActionSideEffectDescriptor d{
                MC::None, ListenerMode::Assistant,
                &MusicSideEffects::on_skip, false};
            return d;
        }
        case ActionKind::AbortReply: {
            // No mode change; the universal stop interrupts whatever
            // the assistant is doing in-place.  The listener
            // additionally fires `barge_.abort_tts_now()` and plays an
            // earcon for immediate auditory acknowledgement.
            static const ActionSideEffectDescriptor d{
                MC::None, ListenerMode::Assistant, nullptr, false, true};
            return d;
        }
        case ActionKind::Sleep: {
            static const ActionSideEffectDescriptor d{
                MC::Enter, ListenerMode::Asleep, nullptr, false, false};
            return d;
        }
        case ActionKind::Wake: {
            // Drop back to home; the listener resolves Asleep→home so
            // sleep on top of an already-asleep listener still wakes.
            static const ActionSideEffectDescriptor d{
                MC::ExitHome, ListenerMode::Asleep, nullptr, false, false};
            return d;
        }
        default:
            return kNoop;
    }
}

} // namespace

const ActionSideEffectDescriptor& descriptor_for(ActionKind kind) {
    return lookup(kind);
}

} // namespace hecquin::voice
