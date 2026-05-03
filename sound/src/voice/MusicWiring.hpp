#pragma once

#include "music/MusicSession.hpp"

#include <memory>

namespace hecquin::music {
class MusicProvider;
}

struct MusicConfig;
class VoiceListener;

namespace hecquin::voice {

/**
 * Owns a `MusicSession` and its provider.  Constructed by
 * `install_music_wiring`; kept alive by the caller for the lifetime of
 * the listener.  Destruction tears down the provider after the session
 * (so any in-flight playback subprocess gets reaped first).
 */
struct MusicWiring {
    std::unique_ptr<hecquin::music::MusicProvider> provider;
    std::unique_ptr<hecquin::music::MusicSession> session;
};

/**
 * Build a music provider from `cfg`, wrap it in a `MusicSession`, and
 * register the music-query callback plus mid-playback controls (abort /
 * pause / resume / volume step / skip) on the listener. Also wires the
 * barge-in controller's gain setter to `MusicProvider::set_gain_target`.
 * Returns the owning bundle.
 *
 * Replaces the copy that was duplicated across voice-listening mains.
 */
MusicWiring install_music_wiring(VoiceListener& listener,
                                 const MusicConfig& cfg);

} // namespace hecquin::voice
