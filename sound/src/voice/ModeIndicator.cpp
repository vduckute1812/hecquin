#include "voice/ModeIndicator.hpp"

namespace hecquin::voice {

void ModeIndicator::notify(ListenerMode mode) {
    if (!earcons_) return;
    // Pick a cue per mode so the user can tell modes apart without
    // counting tones.  Reusing the existing palette keeps the audible
    // vocabulary small (every cue is one more thing to interpret).
    Earcons::Cue cue = Earcons::Cue::StartListening;
    switch (mode) {
        case ListenerMode::Assistant: cue = Earcons::Cue::StartListening; break;
        case ListenerMode::Lesson:    cue = Earcons::Cue::Acknowledge;    break;
        case ListenerMode::Drill:     cue = Earcons::Cue::Wake;           break;
        case ListenerMode::Music:     cue = Earcons::Cue::Thinking;       break;
        case ListenerMode::Asleep:    cue = Earcons::Cue::Sleep;          break;
    }
    earcons_->play_async(cue);
}

} // namespace hecquin::voice
