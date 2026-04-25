// MusicSession exercises a FakeMusicProvider so we can verify:
//   - search failure → MusicPlayback(ok=false) with the "couldn't find"
//     reply.
//   - search success + play success → MusicPlayback(ok=true, title).
//   - search success + play failure → MusicPlayback(ok=false).
//   - empty query bails out without hitting the provider.
//
// No yt-dlp, no ffmpeg, no SDL.  Capture pointer is null so MuteGuard is
// never constructed (which would also need a live AudioCapture).

#include "actions/ActionKind.hpp"
#include "music/MusicProvider.hpp"
#include "music/MusicSession.hpp"

#include <iostream>
#include <optional>
#include <string>

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

class FakeMusicProvider final : public MusicProvider {
public:
    std::optional<MusicTrack> search_result;
    bool play_result = true;
    int  search_calls = 0;
    int  play_calls   = 0;
    std::string last_query;

    std::optional<MusicTrack> search(const std::string& query) override {
        ++search_calls;
        last_query = query;
        return search_result;
    }
    bool play(const MusicTrack&) override {
        ++play_calls;
        return play_result;
    }
    void stop() override {}
};

} // namespace

int main() {
    {
        FakeMusicProvider prov;
        MusicSession s(prov, /*capture=*/nullptr);
        const auto a = s.handle("");
        expect(a.kind == ActionKind::MusicPlayback,
               "empty query -> MusicPlayback");
        expect(prov.search_calls == 0,
               "empty query never calls search");
        expect(a.reply.find("couldn't") != std::string::npos,
               "empty query replies with failure text");
    }

    {
        FakeMusicProvider prov;
        prov.search_result = std::nullopt;
        MusicSession s(prov, nullptr);
        const auto a = s.handle("some unknown song");
        expect(a.kind == ActionKind::MusicPlayback, "search miss kind");
        expect(prov.search_calls == 1 && prov.play_calls == 0,
               "miss searches once and never plays");
        expect(prov.last_query == "some unknown song",
               "query forwarded verbatim after trim");
    }

    {
        FakeMusicProvider prov;
        prov.search_result = MusicTrack{"Bohemian Rhapsody", "http://example"};
        prov.play_result = true;
        MusicSession s(prov, nullptr);
        const auto a = s.handle("   bohemian rhapsody   ");
        expect(a.kind == ActionKind::MusicPlayback, "hit kind");
        expect(prov.search_calls == 1 && prov.play_calls == 1,
               "hit triggers exactly one play");
        expect(prov.last_query == "bohemian rhapsody",
               "whitespace around query is trimmed");
        expect(a.reply.find("Bohemian Rhapsody") != std::string::npos,
               "success reply includes title");
    }

    {
        FakeMusicProvider prov;
        prov.search_result = MusicTrack{"Some Song", "http://example"};
        prov.play_result = false;
        MusicSession s(prov, nullptr);
        const auto a = s.handle("play something");
        expect(a.kind == ActionKind::MusicPlayback, "play-fail kind");
        expect(prov.search_calls == 1 && prov.play_calls == 1,
               "play-fail still calls both");
        expect(a.reply.find("couldn't") != std::string::npos,
               "play-fail reply is apologetic");
    }

    if (failures == 0) {
        std::cout << "[test_music_session] all assertions passed" << std::endl;
        return 0;
    }
    return 1;
}
