#pragma once

#include "actions/Action.hpp"
#include "music/MusicProvider.hpp"

#include <string>

class AudioCapture;

namespace hecquin::music {

/**
 * Facade the voice pipeline talks to once the user has said a song name
 * in `ListenerMode::Music`.  Responsibilities:
 *
 *   - Trim the query, bail out fast on empty input.
 *   - Ask the injected `MusicProvider` to resolve the top track.
 *   - Mute the microphone (via `AudioCapture::MuteGuard`) while the
 *     provider is streaming audio, so the speaker output does not get
 *     recaptured and re-transcribed as noise.
 *   - Return a `MusicPlayback` `Action` that the listener plays as TTS
 *     after playback ends and the mic comes back up.
 *
 * `capture` may be null in tests — in that case we skip muting and just
 * exercise the provider contract.
 */
class MusicSession {
public:
    MusicSession(MusicProvider& provider, AudioCapture* capture)
        : provider_(provider), capture_(capture) {}

    /** Handle one user-provided song query; returns an `Action`. */
    Action handle(const std::string& query);

    /** Forward a stop request to the underlying provider (SIGINT path). */
    void abort();

private:
    MusicProvider& provider_;
    AudioCapture*  capture_;
};

} // namespace hecquin::music
