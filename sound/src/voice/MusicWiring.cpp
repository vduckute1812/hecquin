#include "voice/MusicWiring.hpp"

#include "music/MusicFactory.hpp"
#include "music/MusicProvider.hpp"
#include "voice/VoiceListener.hpp"

namespace hecquin::voice {

MusicWiring install_music_wiring(VoiceListener& listener,
                                 const MusicConfig& cfg) {
    MusicWiring out;
    out.provider = hecquin::music::make_provider_from_config(cfg);
    out.session = std::make_unique<hecquin::music::MusicSession>(*out.provider);

    auto* session = out.session.get();
    listener.setMusicCallback([session](const std::string& q) {
        return session->handle(q);
    });
    // Mid-song voice controls — all forward straight to the session;
    // the listener routes them by ActionKind so the wiring here stays
    // intent-agnostic.
    listener.setMusicAbortCallback ([session]() { session->abort();  });
    listener.setMusicPauseCallback ([session]() { session->pause();  });
    listener.setMusicResumeCallback([session]() { session->resume(); });
    return out;
}

} // namespace hecquin::voice
