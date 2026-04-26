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
 * register all four mid-song callbacks (handle / abort / pause /
 * resume) on the listener.  Returns the owning bundle.
 *
 * Replaces the 15-line copy that was duplicated across the four
 * voice-listening mains.
 */
MusicWiring install_music_wiring(VoiceListener& listener,
                                 const MusicConfig& cfg);

} // namespace hecquin::voice
