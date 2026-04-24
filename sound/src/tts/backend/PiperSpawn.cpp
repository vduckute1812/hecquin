#include "tts/backend/PiperSpawn.hpp"

#include "tts/runtime/PiperRuntime.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
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

// Close both ends of a pipe that the helper owns locally (e.g. after a
// setup failure).  Ignores -1 sentinels.
void close_pipe_pair(int p[2]) {
    if (p[0] >= 0) close(p[0]);
    if (p[1] >= 0) close(p[1]);
}

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

    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
        result.error_reason = std::string("pipe() failed: ") + std::strerror(errno);
        std::cerr << "[piper] " << result.error_reason << std::endl;
        close_pipe_pair(in_pipe);
        close_pipe_pair(out_pipe);
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in_pipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, in_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, in_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, out_pipe[1]);

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

    // Parent no longer needs the child's ends.
    close(in_pipe[0]);
    close(out_pipe[1]);

    if (spawn_rc != 0) {
        result.error_reason = std::string("posix_spawnp failed: ") + std::strerror(spawn_rc);
        std::cerr << "[piper] " << result.error_reason << std::endl;
        close(in_pipe[1]);
        close(out_pipe[0]);
        return result;
    }

    result.spawned = true;

    write_text_then_close(in_pipe[1], text);

    constexpr std::size_t kChunk = 4096;
    char buf[kChunk];
    bool keep_reading = true;
    for (;;) {
        const ssize_t r = read(out_pipe[0], buf, sizeof(buf));
        if (r < 0) {
            if (errno == EINTR) continue;
            result.error_reason = std::string("read() failed: ") + std::strerror(errno);
            std::cerr << "[piper] " << result.error_reason << std::endl;
            break;
        }
        if (r == 0) break; // EOF
        if (keep_reading) {
            const std::size_t n_samples = static_cast<std::size_t>(r) / sizeof(std::int16_t);
            const auto* p = reinterpret_cast<const std::int16_t*>(buf);
            keep_reading = on_samples(p, n_samples);
        }
        // We keep draining the pipe even if the consumer said "stop" so
        // piper can flush and exit cleanly.
    }
    close(out_pipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    result.exit_status = status;
    return result;
}

} // namespace hecquin::tts::backend
