#include "music/MusicSession.hpp"

#include "actions/MusicAction.hpp"
#include "common/StringUtils.hpp"

#include <iostream>
#include <optional>
#include <utility>

namespace hecquin::music {

using hecquin::common::trim_copy;

MusicSession::~MusicSession() {
    abort();
}

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

    start_playback_thread_(*track);

    return MusicAction::playback(trimmed, /*ok=*/true, track->title);
}

void MusicSession::start_playback_thread_(const MusicTrack& track) {
    // Hand playback off to a background thread so the listener loop can
    // keep capturing voice and route subsequent commands ("stop music",
    // "pause music", …) while the song streams.  Any in-flight song
    // from a previous `handle()` call is aborted+joined first; only one
    // song plays at a time.
    std::lock_guard<std::mutex> lk(thread_mu_);
    abort_locked_();

    playing_.store(true, std::memory_order_release);
    playback_thread_ = std::thread([this, t = track]() {
        // Provider failures land in stderr inside the provider itself;
        // the user has already heard the "Now playing …" TTS reply by
        // the time `play()` is called, so a streaming failure mid-song
        // just shortens the song.
        const bool ok = provider_.play(t);
        (void) ok;
        playing_.store(false, std::memory_order_release);
    });
}

void MusicSession::abort() {
    std::lock_guard<std::mutex> lk(thread_mu_);
    abort_locked_();
}

void MusicSession::pause() {
    if (playing_.load(std::memory_order_acquire)) {
        provider_.pause();
    }
}

void MusicSession::resume() {
    if (playing_.load(std::memory_order_acquire)) {
        provider_.resume();
    }
}

void MusicSession::step_volume(float delta) {
    if (playing_.load(std::memory_order_acquire)) {
        // 80 ms cross-fade matches the duck/un-duck ramp used by the
        // barge-in controller — keeps the UX consistent.
        provider_.step_volume(delta, /*ramp_ms=*/80);
    }
}

void MusicSession::skip() {
    // The default provider implementation falls back to `stop()`,
    // which interrupts `play()` and lets the background worker exit.
    // Joining the worker (so a subsequent `handle()` doesn't double up)
    // is `abort()`'s job; do that here as well so the listener can
    // immediately route a new search prompt.
    if (playing_.load(std::memory_order_acquire)) {
        provider_.skip();
        abort();
    }
}

void MusicSession::abort_locked_() {
    if (playback_thread_.joinable()) {
        // `provider_.stop()` is idempotent and safe to call from a
        // different thread than the one running `play()`.  It signals
        // the worker to drop out of its read loop; the join below
        // guarantees the thread is fully reaped before we return —
        // which means by the time `handle()` resolves the previous
        // song, the audio device is definitely closed.
        provider_.stop();
        playback_thread_.join();
    }
    playing_.store(false, std::memory_order_release);
}

} // namespace hecquin::music
