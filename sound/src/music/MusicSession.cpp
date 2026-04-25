#include "music/MusicSession.hpp"

#include "actions/MusicAction.hpp"
#include "common/StringUtils.hpp"
#include "voice/AudioCapture.hpp"

#include <iostream>
#include <optional>

namespace hecquin::music {

using hecquin::common::trim_copy;

Action MusicSession::handle(const std::string& query) {
    const std::string trimmed = trim_copy(query);
    if (trimmed.empty()) {
        return MusicAction::playback(query, /*ok=*/false, /*title=*/"");
    }

    std::cout << "[music] searching for: " << trimmed << std::endl;
    const std::optional<MusicTrack> track = provider_.search(trimmed);
    if (!track) {
        std::cerr << "[music] no match for \"" << trimmed << "\"" << std::endl;
        return MusicAction::playback(trimmed, /*ok=*/false, /*title=*/"");
    }

    std::cout << "[music] playing: " << track->title
              << "  (" << track->url << ")" << std::endl;

    bool ok = false;
    if (capture_) {
        // MuteGuard pauses + clears the mic ring for the full duration
        // of playback, then restores it on scope exit — exactly the
        // invariant the drill / TTS paths already rely on.
        AudioCapture::MuteGuard mute(*capture_);
        ok = provider_.play(*track);
    } else {
        ok = provider_.play(*track);
    }
    return MusicAction::playback(trimmed, ok, track->title);
}

void MusicSession::abort() {
    provider_.stop();
}

} // namespace hecquin::music
