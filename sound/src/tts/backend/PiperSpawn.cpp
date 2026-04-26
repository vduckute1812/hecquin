#include "tts/backend/PiperSpawn.hpp"

#include "tts/runtime/PiperRuntime.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

extern char** environ;

#ifndef PIPER_EXECUTABLE
#define PIPER_EXECUTABLE "piper"
#endif

namespace hecquin::tts::backend {

bool PiperSpawnResult::ok_exit() const {
    return spawned && WIFEXITED(exit_status) && WEXITSTATUS(exit_status) == 0;
}

namespace {

/**
 * Owning RAII wrapper for a single pipe fd.  Replaces the scattered
 * manual `close()` calls in `run_pipe_synth`; call `release()` to hand
 * ownership back to the caller before destruction.
 */
class PipeFdGuard {
public:
    PipeFdGuard() = default;
    explicit PipeFdGuard(int fd) : fd_(fd) {}
    ~PipeFdGuard() { reset(); }

    PipeFdGuard(const PipeFdGuard&) = delete;
    PipeFdGuard& operator=(const PipeFdGuard&) = delete;

    PipeFdGuard(PipeFdGuard&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    PipeFdGuard& operator=(PipeFdGuard&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int  get() const noexcept    { return fd_; }
    bool valid() const noexcept  { return fd_ >= 0; }
    int  release() noexcept      { int t = fd_; fd_ = -1; return t; }
    void reset() noexcept        { if (fd_ >= 0) ::close(fd_); fd_ = -1; }

private:
    int fd_{-1};
};

struct PipePair {
    PipeFdGuard read;
    PipeFdGuard write;
};

/** Open parent ↔ child stdin/stdout pipes; logs and returns `nullopt`
 *  on `pipe(2)` failure (no fd leaks). */
std::optional<std::pair<PipePair, PipePair>> setup_stdin_stdout_pipes() {
    int in_raw[2]  = {-1, -1};
    int out_raw[2] = {-1, -1};
    if (pipe(in_raw) != 0 || pipe(out_raw) != 0) {
        std::cerr << "[piper] pipe() failed: " << std::strerror(errno) << std::endl;
        if (in_raw[0]  >= 0) close(in_raw[0]);
        if (in_raw[1]  >= 0) close(in_raw[1]);
        if (out_raw[0] >= 0) close(out_raw[0]);
        if (out_raw[1] >= 0) close(out_raw[1]);
        return std::nullopt;
    }
    return std::make_pair(
        PipePair{PipeFdGuard(in_raw[0]),  PipeFdGuard(in_raw[1])},
        PipePair{PipeFdGuard(out_raw[0]), PipeFdGuard(out_raw[1])});
}

/**
 * `posix_spawnp` the piper child wired to the given stdin read fd and
 * stdout write fd.  Caller keeps the other halves for the parent.
 * Returns child pid (or `nullopt` + log on spawn failure).
 */
std::optional<pid_t> spawn_piper_child(const std::string& model_path,
                                       int stdin_read_fd, int stdout_write_fd,
                                       int stdin_write_fd, int stdout_read_fd) {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdin_read_fd,    STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stdout_write_fd,  STDOUT_FILENO);
    // Close the parent-side ends in the child so it doesn't keep the
    // pipes open after exec.
    posix_spawn_file_actions_addclose(&actions, stdin_write_fd);
    posix_spawn_file_actions_addclose(&actions, stdout_read_fd);
    posix_spawn_file_actions_addclose(&actions, stdin_read_fd);
    posix_spawn_file_actions_addclose(&actions, stdout_write_fd);

    const std::string exe = PIPER_EXECUTABLE;
    const std::string model_arg = model_path;
    std::vector<char*> argv = {
        const_cast<char*>(exe.c_str()),
        const_cast<char*>("--model"),
        const_cast<char*>(model_arg.c_str()),
        const_cast<char*>("--output_raw"),
        nullptr,
    };

    pid_t pid = 0;
    const int spawn_rc =
        posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);

    if (spawn_rc != 0) {
        std::cerr << "[piper] posix_spawnp failed: " << std::strerror(spawn_rc) << std::endl;
        return std::nullopt;
    }
    return pid;
}

/** Push `text + '\n'` into the child's stdin and close. */
void write_text_then_close(int fd, const std::string& text) {
    if (!text.empty()) {
        const char* buf = text.data();
        std::size_t left = text.size();
        while (left > 0) {
            const ssize_t w = write(fd, buf, left);
            if (w <= 0) {
                if (errno == EINTR) continue;
                break;
            }
            buf += w;
            left -= static_cast<std::size_t>(w);
        }
    }
    const char nl = '\n';
    while (write(fd, &nl, 1) < 0 && errno == EINTR) {
    }
    close(fd);
}

/**
 * Drain piper's stdout, forwarding raw int16 PCM samples to
 * `on_samples`.  Returns an error reason on `read(2)` failure.  Keeps
 * draining after the consumer signals "stop" so piper can flush and
 * exit on its own.
 */
std::string pump_stdout(int read_fd,
                        const std::function<bool(const std::int16_t*, std::size_t)>& on_samples) {
    constexpr std::size_t kChunk = 4096;
    char buf[kChunk];
    bool keep_reading = true;
    for (;;) {
        const ssize_t r = read(read_fd, buf, sizeof(buf));
        if (r < 0) {
            if (errno == EINTR) continue;
            const std::string reason =
                std::string("read() failed: ") + std::strerror(errno);
            std::cerr << "[piper] " << reason << std::endl;
            return reason;
        }
        if (r == 0) break; // EOF
        if (keep_reading) {
            const std::size_t n_samples = static_cast<std::size_t>(r) / sizeof(std::int16_t);
            const auto* p = reinterpret_cast<const std::int16_t*>(buf);
            keep_reading = on_samples(p, n_samples);
        }
    }
    return {};
}

/** `waitpid` retrying through `EINTR`. */
int reap_child(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return status;
}

} // namespace

PiperSpawnResult run_pipe_synth(
    const std::string& text,
    const std::string& model_path,
    const std::function<bool(const std::int16_t*, std::size_t)>& on_samples) {
    PiperSpawnResult result;

    if (!std::filesystem::exists(model_path)) {
        result.error_reason = "model not found: " + model_path;
        std::cerr << "[piper] " << result.error_reason << std::endl;
        return result;
    }

    runtime::configure();

    auto pipes = setup_stdin_stdout_pipes();
    if (!pipes) {
        result.error_reason = std::string("pipe() failed: ") + std::strerror(errno);
        return result;
    }
    auto& [in_pipe, out_pipe] = *pipes;

    auto pid = spawn_piper_child(model_path,
                                 in_pipe.read.get(),  out_pipe.write.get(),
                                 in_pipe.write.get(), out_pipe.read.get());
    // Parent no longer needs the child's ends regardless of spawn outcome.
    in_pipe.read.reset();
    out_pipe.write.reset();

    if (!pid) {
        result.error_reason = std::string("posix_spawnp failed: ") + std::strerror(errno);
        return result;
    }

    result.spawned = true;
    write_text_then_close(in_pipe.write.release(), text);

    if (auto err = pump_stdout(out_pipe.read.get(), on_samples); !err.empty()) {
        result.error_reason = std::move(err);
    }
    out_pipe.read.reset();

    result.exit_status = reap_child(*pid);
    return result;
}

} // namespace hecquin::tts::backend
