#pragma once

#include "actions/ActionKind.hpp"
#include "voice/ListenerMode.hpp"
#include "voice/MusicSideEffects.hpp"

namespace hecquin::voice {

/**
 * Per-`ActionKind` recipe for the small mode-change + music side
 * effect that `VoiceListener::apply_local_intent_side_effects_` used
 * to express as a `switch`.
 *
 * Adding a new local intent now means adding a row to the registry's
 * descriptor table — no switch case, no extra plumbing in the listener
 * itself.
 */
struct ActionSideEffectDescriptor {
    enum class ModeChange {
        None,           ///< Leave `mode_` untouched.
        EnterIfEnable,  ///< action.enable ? mode_=mode_value : exit_target(mode_value)
        ExitTo,         ///< mode_ = exit_target(mode_value)
        Enter,          ///< mode_ = mode_value
        ExitHome,       ///< mode_ = home_mode_ (used by Wake / abort).
    };

    ModeChange   mode_change = ModeChange::None;
    /// Interpreted by `mode_change`: target of an `Enter` / sub-mode of
    /// an `ExitTo` / `EnterIfEnable`.
    ListenerMode mode_value{};

    /**
     * Optional pointer-to-member hook on the listener's
     * `MusicSideEffects` instance.  `nullptr` skips the call.
     */
    void (MusicSideEffects::*music_hook)() = nullptr;

    /// Drill-mode toggles set this so the listener queues an opening
    /// announcement after entering the mode.
    bool sets_pending_drill_from_enable = false;

    /// When true, the listener fires `barge.abort_tts_now()` on this
    /// action.  Used by the universal `AbortReply` intent so the
    /// in-flight reply is cut short *before* the new reply is spoken.
    bool aborts_tts = false;
};

/// Look up the descriptor for `kind`.  Always returns a valid (possibly
/// all-default = no-op) descriptor so the caller doesn't need
/// fall-through handling.
const ActionSideEffectDescriptor& descriptor_for(ActionKind kind);

} // namespace hecquin::voice
