// MusicSession exercises a FakeMusicProvider so we can verify:
//   - empty query → MusicNotFound without hitting the provider.
//   - search failure → MusicNotFound with the "couldn't find" reply.
//   - search success → MusicPlayback(title) returns immediately while
//     play() runs on a background thread (the new async contract).
//   - abort() during playback breaks the provider's blocking play() via
//     stop() and joins the worker thread cleanly.
//   - pause() / resume() forward through the session only while a song is
//     actually playing.
//
// No yt-dlp, no ffmpeg, no SDL.

#include "actions/ActionKind.hpp"
#include "music/MusicProvider.hpp"
#include "music/MusicSession.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

using hecquin::music::MusicProvider;
using hecquin::music::MusicSession;
using hecquin::music::MusicTrack;

namespace {

int failures = 0;

void expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "[FAIL] " << label << std::endl;
        ++failures;
    }
}

/**
 * Provider whose `play()` blocks on a condvar until either `stop()` is
 * called or the test explicitly releases it.  Lets us drive the
 * MusicSession async contract deterministically without sleeps.
 */
class FakeMusicProvider final : public MusicProvider {
public:
    std::optional<MusicTrack> search_result;
    bool play_result = true;
    /** When true, `play()` blocks until `stop()` is called or the test
     *  flips `release` directly.  When false, `play()` returns
     *  `play_result` immediately. */
    bool blocking_play = false;

    std::atomic<int> search_calls{0};
    std::atomic<int> play_calls{0};
    std::atomic<int> stop_calls{0};
    std::atomic<int> pause_calls{0};
    std::atomic<int> resume_calls{0};
    std::string last_query;

    std::optional<MusicTrack> search(const std::string& query) override {
        ++search_calls;
        last_query = query;
        return search_result;
    }
    bool play(const MusicTrack&) override {
        ++play_calls;
        if (!blocking_play) return play_result;
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this]() { return release_.load(); });
        return play_result;
    }
    void stop() override {
        ++stop_calls;
        release_.store(true);
        cv_.notify_all();
    }
    void pause() override { ++pause_calls; }
    void resume() override { ++resume_calls; }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> release_{false};
};

bool wait_for(std::atomic<int>& counter, int target,
              std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (counter.load() < target) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

} // namespace

int main() {
    {
        FakeMusicProvider prov;
        MusicSession s(prov);
        const auto a = s.handle("");
        expect(a.kind == ActionKind::MusicNotFound,
               "empty query -> MusicNotFound");
        expect(prov.search_calls.load() == 0,
               "empty query never calls search");
        expect(a.reply.find("couldn't") != std::string::npos,
               "empty query replies with failure text");
    }

    {
        FakeMusicProvider prov;
        prov.search_result = std::nullopt;
        MusicSession s(prov);
        const auto a = s.handle("some unknown song");
        expect(a.kind == ActionKind::MusicNotFound, "search miss kind");
        expect(prov.search_calls.load() == 1 && prov.play_calls.load() == 0,
               "miss searches once and never plays");
        expect(prov.last_query == "some unknown song",
               "query forwarded verbatim after trim");
        expect(!s.is_playing(), "miss leaves session idle");
    }

    {
        FakeMusicProvider prov;
        prov.search_result = MusicTrack{"Bohemian Rhapsody", "http://example"};
        prov.play_result = true;
        MusicSession s(prov);
        const auto a = s.handle("   bohemian rhapsody   ");
        expect(a.kind == ActionKind::MusicPlayback, "hit kind");
        expect(a.reply.find("Bohemian Rhapsody") != std::string::npos,
               "success reply includes title");
        expect(prov.last_query == "bohemian rhapsody",
               "whitespace around query is trimmed");

        // Async playback: handle() returned immediately; the worker
        // thread eventually calls play() exactly once.
        expect(wait_for(prov.play_calls, 1, std::chrono::seconds(2)),
               "background thread reaches play() within 2 s");
        expect(prov.search_calls.load() == 1,
               "hit searches exactly once");
    }

    {
        // Abort path: blocking play() stays parked until session.abort()
        // calls provider.stop() and joins the worker thread.
        FakeMusicProvider prov;
        prov.search_result = MusicTrack{"Endless Loop", "http://example"};
        prov.blocking_play = true;
        MusicSession s(prov);

        const auto a = s.handle("endless loop");
        expect(a.kind == ActionKind::MusicPlayback, "abort: hit kind");
        expect(wait_for(prov.play_calls, 1, std::chrono::seconds(2)),
               "abort: play() entered before abort");
        expect(s.is_playing(),
               "session reports playing while play() is parked");

        s.abort();
        expect(prov.stop_calls.load() >= 1,
               "abort() calls provider.stop()");
        expect(!s.is_playing(),
               "abort() flips is_playing back to false");
    }

    {
        // pause / resume short-circuit when nothing is playing, and
        // forward to the provider while a song is parked.
        FakeMusicProvider prov;
        prov.search_result = MusicTrack{"Pausable", "http://example"};
        prov.blocking_play = true;
        MusicSession s(prov);

        s.pause();
        s.resume();
        expect(prov.pause_calls.load() == 0 && prov.resume_calls.load() == 0,
               "pause/resume are no-ops when nothing is playing");

        const auto a = s.handle("pausable");
        (void) a;
        expect(wait_for(prov.play_calls, 1, std::chrono::seconds(2)),
               "pausable: play() entered");

        s.pause();
        s.resume();
        expect(prov.pause_calls.load() == 1 && prov.resume_calls.load() == 1,
               "pause/resume forward to provider while playing");

        s.abort();
    }

    {
        // Starting a new song aborts the previous one.
        FakeMusicProvider prov;
        prov.search_result = MusicTrack{"First", "http://a"};
        prov.blocking_play = true;
        MusicSession s(prov);

        s.handle("first");
        expect(wait_for(prov.play_calls, 1, std::chrono::seconds(2)),
               "first play() entered");

        prov.search_result = MusicTrack{"Second", "http://b"};
        s.handle("second");
        expect(prov.stop_calls.load() >= 1,
               "switching songs aborts the previous worker via stop()");
        expect(wait_for(prov.play_calls, 2, std::chrono::seconds(2)),
               "second play() entered after the first was joined");

        s.abort();
    }

    if (failures == 0) {
        std::cout << "[test_music_session] all assertions passed" << std::endl;
        return 0;
    }
    return 1;
}
