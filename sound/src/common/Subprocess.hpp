#pragma once

#include <sys/types.h>

#include <cstddef>
#include <string>

namespace hecquin::common {

/**
 * RAII handle for a child process spawned via `/bin/sh -c <cmd>` whose
 * stdout is plumbed back to the parent through a pipe.
 *
 * Replaces the ad-hoc `Spawned` struct + `spawn_read` / `reap` helpers
 * that used to live inside `music/YouTubeMusicProvider.cpp`.  Lifting
 * the logic out lets every subprocess-driving subsystem (yt-dlp, ffmpeg,
 * future plug-ins) share the same lifecycle guarantees:
 *
 *   - `spawn_read` always either returns a valid `(pid, fd)` pair or a
 *     "not started" instance whose `valid()` returns false.
 *   - The destructor closes the read fd (if still open) and reaps the
 *     child (graceful SIGTERM with a 0.5 s grace period, then SIGKILL).
 *     Calling `kill_and_reap()` early is safe and idempotent.
 *
 * Why `/bin/sh -c` rather than `posix_spawnp`: callers in `music/`
 * intentionally build pipeline commands like `yt-dlp ... | ffmpeg ...`
 * which are simplest to express as a shell string.  `tts/backend/PiperSpawn`
 * keeps using `posix_spawnp` for the no-shell path it already needed.
 */
class Subprocess {
public:
    Subprocess() = default;
    ~Subprocess();

    Subprocess(const Subprocess&)            = delete;
    Subprocess& operator=(const Subprocess&) = delete;
    Subprocess(Subprocess&& other) noexcept;
    Subprocess& operator=(Subprocess&& other) noexcept;

    /**
     * Launch `cmd` under `/bin/sh -c` with stdout redirected to a pipe
     * the parent can read from `stdout_fd()`.  stderr stays attached to
     * the parent's tty so subprocess errors / progress are visible to
     * the operator.
     *
     * Returns a started instance on success; `valid()` is false on any
     * `pipe()` / `fork()` / `execl()` failure.  Failures emit a single
     * `[subprocess]` warning to stderr.
     */
    static Subprocess spawn_read(const std::string& cmd);

    bool   valid() const noexcept { return pid_ > 0 && stdout_fd_ >= 0; }
    pid_t  pid() const noexcept   { return pid_; }
    int    stdout_fd() const noexcept { return stdout_fd_; }

    /**
     * Read up to `buf_size` bytes from the child's stdout into `buf`.
     * Returns the number of bytes read, 0 on EOF, or -1 on a non-EINTR
     * read error (with errno preserved).  EINTR is retried internally.
     */
    long read_some(void* buf, std::size_t buf_size);

    /** Send SIGTERM to the child without waiting.  Idempotent. */
    void send_sigterm();

    /**
     * Close the stdout fd and reap the child.  The child gets up to
     * 0.5 s to exit on its own (or after the SIGTERM `send_sigterm`
     * delivered) before SIGKILL is escalated.  Returns the child's
     * `waitpid` status (0 if the process was already reaped or never
     * started).  Idempotent and safe to call from the destructor.
     */
    int kill_and_reap();

    /**
     * Detach the stdout fd ownership to the caller.  The caller is
     * responsible for `::close()` on the returned fd.  Returns -1 if no
     * fd is owned.  After detach, `stdout_fd()` returns -1 and the
     * destructor will not close the fd.
     */
    int detach_stdout_fd() noexcept;

private:
    pid_t pid_       = -1;
    int   stdout_fd_ = -1;
};

} // namespace hecquin::common
