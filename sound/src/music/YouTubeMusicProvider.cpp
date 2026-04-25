#include "music/YouTubeMusicProvider.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>

namespace hecquin::music {

namespace {

// Single-quote escaping for POSIX `/bin/sh`: close the quoted string,
// insert an escaped single quote, reopen.  Safe for any payload.
std::string shell_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') {
            out.append("'\\''");
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

bool file_exists(const std::string& path) {
    if (path.empty()) return false;
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

// Build the `--cookies` fragment (already shell-safe) or an empty string.
std::string cookies_arg(const std::string& cookies_file) {
    if (cookies_file.empty() || !file_exists(cookies_file)) return {};
    return " --cookies " + shell_escape(cookies_file);
}

/**
 * Launch `cmd` under `/bin/sh -c` and return both the child pid and a
 * read-only file descriptor hooked to its stdout.  `popen("r")` is
 * equivalent but hides the pid from us, so we can't kill the pipeline
 * out-of-band.
 */
struct Spawned {
    pid_t pid = -1;
    int   fd  = -1;
};

Spawned spawn_read(const std::string& cmd) {
    int pipefd[2];
    if (::pipe(pipefd) != 0) {
        std::cerr << "[music] pipe() failed: " << std::strerror(errno) << std::endl;
        return {};
    }
    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        std::cerr << "[music] fork() failed: " << std::strerror(errno) << std::endl;
        return {};
    }
    if (pid == 0) {
        // Child: redirect stdout to the pipe write end, close unused fds.
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::close(pipefd[1]);
        // stderr stays attached to the parent's tty so yt-dlp / ffmpeg
        // progress + errors are visible to the operator.
        ::execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        // Only reached if execl itself fails.
        std::cerr << "[music] execl failed: " << std::strerror(errno) << std::endl;
        ::_exit(127);
    }
    ::close(pipefd[1]);
    return Spawned{pid, pipefd[0]};
}

// Kill the child politely (SIGTERM), then SIGKILL after a short grace
// period if it's still alive.  Always reaps the zombie.
int reap(pid_t pid) {
    if (pid <= 0) return 0;
    int status = 0;
    // Non-blocking first check.
    if (::waitpid(pid, &status, WNOHANG) == pid) return status;
    ::kill(pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        if (::waitpid(pid, &status, WNOHANG) == pid) return status;
        ::usleep(25 * 1000);
    }
    ::kill(pid, SIGKILL);
    ::waitpid(pid, &status, 0);
    return status;
}

} // namespace

void YouTubeMusicConfig::apply_env_overrides() {
    auto read = [](const char* name, std::string& out) {
        const char* v = std::getenv(name);
        if (v && *v) out = v;
    };
    read("HECQUIN_YT_DLP_BIN", yt_dlp_binary);
    read("HECQUIN_FFMPEG_BIN", ffmpeg_binary);
    read("HECQUIN_YT_COOKIES_FILE", cookies_file);
    const char* rate = std::getenv("HECQUIN_MUSIC_SAMPLE_RATE");
    if (rate && *rate) {
        try { sample_rate_hz = std::max(8000, std::stoi(rate)); } catch (...) {}
    }
}

YouTubeMusicProvider::YouTubeMusicProvider(YouTubeMusicConfig cfg)
    : cfg_(std::move(cfg)) {}

YouTubeMusicProvider::~YouTubeMusicProvider() {
    stop();
}

std::optional<MusicTrack> YouTubeMusicProvider::search(const std::string& query) {
    if (query.empty()) return std::nullopt;

    // `ytsearch1:` = top 1 hit.  Appending "music" biases YouTube's own
    // relevance ranker towards songs vs talks / video essays, which
    // matches user intent ("open music → song name").
    const std::string q = "ytsearch1:" + query + " music";

    // NOTE: the `--print` template MUST contain a real TAB byte (0x09).
    // yt-dlp does not expand backslash escapes inside `--print`, so writing
    // `'%(title)s\t%(webpage_url)s'` ships a literal `\` `t` to yt-dlp and
    // we never find a tab to split on.  Embed the tab via `shell_escape`,
    // which wraps the string in single quotes — POSIX sh preserves the
    // raw byte verbatim across the quote.
    const std::string print_fmt = std::string("%(title)s") + '\t' + "%(webpage_url)s";

    std::ostringstream cmd;
    cmd << shell_escape(cfg_.yt_dlp_binary)
        << " --no-warnings --no-playlist"
        << " --default-search ytsearch"
        << " --print " << shell_escape(print_fmt)
        << cookies_arg(cfg_.cookies_file)
        << " " << shell_escape(q);

    auto sp = spawn_read(cmd.str());
    if (sp.pid < 0) return std::nullopt;

    std::string buf;
    std::array<char, 1024> chunk{};
    for (;;) {
        const ssize_t n = ::read(sp.fd, chunk.data(), chunk.size());
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        buf.append(chunk.data(), static_cast<std::size_t>(n));
    }
    ::close(sp.fd);
    const int status = reap(sp.pid);
    (void) status;

    // Keep the first non-empty line only; yt-dlp occasionally prints
    // progress hints before the `--print` payload.
    std::string line;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        const char c = buf[i];
        if (c == '\n') {
            if (!line.empty()) break;
        } else {
            line.push_back(c);
        }
    }
    if (line.empty()) return std::nullopt;

    // Primary separator is a real TAB; fall back to a literal "\\t" only
    // to survive yt-dlp regressions where escapes aren't expanded.  Surface
    // a parse failure clearly so silent misses can't hide as "no match".
    auto sep = line.find('\t');
    std::size_t sep_len = 1;
    if (sep == std::string::npos) {
        sep = line.find("\\t");
        if (sep != std::string::npos) sep_len = 2;
    }
    if (sep == std::string::npos) {
        std::cerr << "[music] unparsable yt-dlp line: " << line << std::endl;
        return std::nullopt;
    }
    MusicTrack t;
    t.title = line.substr(0, sep);
    t.url   = line.substr(sep + sep_len);
    if (t.url.empty()) return std::nullopt;
    return t;
}

bool YouTubeMusicProvider::play(const MusicTrack& track) {
    if (track.url.empty()) return false;
    aborted_.store(false);

    std::ostringstream cmd;
    cmd << shell_escape(cfg_.yt_dlp_binary)
        << " -q --no-warnings --no-playlist -f bestaudio -o -"
        << cookies_arg(cfg_.cookies_file)
        << " " << shell_escape(track.url)
        << " | " << shell_escape(cfg_.ffmpeg_binary)
        << " -hide_banner -loglevel error -i pipe:0"
        << " -f s16le -ac 1 -ar " << cfg_.sample_rate_hz
        << " pipe:1";

    auto sp = spawn_read(cmd.str());
    if (sp.pid < 0) return false;
    child_pid_.store(sp.pid);

    // Streaming player drives SDL.  Construct fresh per track so we can
    // tear it down cleanly after each song (SDL keeps the device handle).
    player_ = std::make_unique<hecquin::tts::playback::StreamingSdlPlayer>();
    if (!player_->start(cfg_.sample_rate_hz)) {
        std::cerr << "[music] failed to open SDL audio device" << std::endl;
        ::close(sp.fd);
        reap(sp.pid);
        child_pid_.store(0);
        player_.reset();
        return false;
    }

    bool got_audio = false;
    std::array<std::int16_t, 4096> samples{};
    // Read raw s16le from the ffmpeg stdout end of the pipeline in
    // fixed-size chunks and push into the player.  Short reads are fine
    // as long as they're a multiple of sizeof(int16_t) — on POSIX that
    // is guaranteed for blocking read on a pipe.
    for (;;) {
        const ssize_t bytes = ::read(
            sp.fd, samples.data(), samples.size() * sizeof(std::int16_t));
        if (bytes < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (bytes == 0) break;
        const std::size_t n = static_cast<std::size_t>(bytes) / sizeof(std::int16_t);
        if (n == 0) continue;
        got_audio = true;
        player_->push(samples.data(), n);
        if (aborted_.load()) break;
    }

    ::close(sp.fd);
    reap(sp.pid);
    child_pid_.store(0);

    player_->finish();
    player_->wait_until_drained();
    player_->stop();
    player_.reset();

    return got_audio && !aborted_.load();
}

void YouTubeMusicProvider::stop() {
    aborted_.store(true);
    const int pid = child_pid_.exchange(0);
    if (pid > 0) {
        ::kill(pid, SIGTERM);
    }
    if (player_) {
        player_->finish();
        player_->stop();
    }
}

} // namespace hecquin::music
