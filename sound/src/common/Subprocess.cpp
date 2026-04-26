#include "common/Subprocess.hpp"

#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

namespace hecquin::common {

namespace {

// Quick non-blocking reap; returns true if the child has already exited.
bool try_reap_nonblocking(pid_t pid, int& status) {
    return ::waitpid(pid, &status, WNOHANG) == pid;
}

} // namespace

Subprocess::~Subprocess() {
    kill_and_reap();
}

Subprocess::Subprocess(Subprocess&& other) noexcept
    : pid_(other.pid_), stdout_fd_(other.stdout_fd_) {
    other.pid_ = -1;
    other.stdout_fd_ = -1;
}

Subprocess& Subprocess::operator=(Subprocess&& other) noexcept {
    if (this != &other) {
        kill_and_reap();
        pid_ = other.pid_;
        stdout_fd_ = other.stdout_fd_;
        other.pid_ = -1;
        other.stdout_fd_ = -1;
    }
    return *this;
}

Subprocess Subprocess::spawn_read(const std::string& cmd) {
    Subprocess sp;

    int pipefd[2];
    if (::pipe(pipefd) != 0) {
        std::cerr << "[subprocess] pipe() failed: "
                  << std::strerror(errno) << std::endl;
        return sp;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        std::cerr << "[subprocess] fork() failed: "
                  << std::strerror(errno) << std::endl;
        return sp;
    }
    if (pid == 0) {
        // Child: hook stdout to the pipe write end, leave stderr alone
        // so progress / errors stay visible to the operator's tty.
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::close(pipefd[1]);
        ::execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        std::cerr << "[subprocess] execl failed: "
                  << std::strerror(errno) << std::endl;
        ::_exit(127);
    }

    ::close(pipefd[1]);
    sp.pid_       = pid;
    sp.stdout_fd_ = pipefd[0];
    return sp;
}

long Subprocess::read_some(void* buf, std::size_t buf_size) {
    if (stdout_fd_ < 0) return -1;
    for (;;) {
        const ssize_t n = ::read(stdout_fd_, buf, buf_size);
        if (n < 0 && errno == EINTR) continue;
        return static_cast<long>(n);
    }
}

void Subprocess::send_sigterm() {
    if (pid_ > 0) {
        ::kill(pid_, SIGTERM);
    }
}

int Subprocess::kill_and_reap() {
    if (stdout_fd_ >= 0) {
        ::close(stdout_fd_);
        stdout_fd_ = -1;
    }
    if (pid_ <= 0) return 0;

    int status = 0;
    if (try_reap_nonblocking(pid_, status)) {
        pid_ = -1;
        return status;
    }
    ::kill(pid_, SIGTERM);
    // Up to ~0.5 s of grace at 25 ms granularity before escalating.
    for (int i = 0; i < 20; ++i) {
        if (try_reap_nonblocking(pid_, status)) {
            pid_ = -1;
            return status;
        }
        ::usleep(25 * 1000);
    }
    ::kill(pid_, SIGKILL);
    ::waitpid(pid_, &status, 0);
    pid_ = -1;
    return status;
}

int Subprocess::detach_stdout_fd() noexcept {
    const int fd = stdout_fd_;
    stdout_fd_ = -1;
    return fd;
}

} // namespace hecquin::common
