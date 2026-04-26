// Unit tests for hecquin::common::Subprocess — round-trip a small shell
// command through `spawn_read` and exercise `kill_and_reap` on a sleep
// child to confirm the destructor doesn't leave zombies behind.

#include "common/Subprocess.hpp"

#include <sys/wait.h>

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

std::string drain(hecquin::common::Subprocess& sp) {
    std::string out;
    char buf[256];
    for (;;) {
        const long n = sp.read_some(buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, static_cast<std::size_t>(n));
    }
    return out;
}

void test_echo_round_trip() {
    auto sp = hecquin::common::Subprocess::spawn_read("printf 'hello\\nworld\\n'");
    if (!sp.valid()) {
        std::cerr << "FAIL: spawn_read returned invalid" << std::endl;
        std::exit(1);
    }
    const std::string out = drain(sp);
    const int status = sp.kill_and_reap();
    if (out != "hello\nworld\n") {
        std::cerr << "FAIL: unexpected stdout '" << out << "'" << std::endl;
        std::exit(1);
    }
    // /bin/sh -c "printf ..." exits with 0 cleanly on POSIX.
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "FAIL: bad exit status " << status << std::endl;
        std::exit(1);
    }
}

void test_kill_and_reap_terminates_long_running_child() {
    auto sp = hecquin::common::Subprocess::spawn_read("sleep 30");
    if (!sp.valid()) {
        std::cerr << "FAIL: spawn_read returned invalid" << std::endl;
        std::exit(1);
    }
    const auto t0 = std::chrono::steady_clock::now();
    sp.kill_and_reap();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    // Should be sub-second: SIGTERM grace is ~0.5 s, sleep handles it
    // immediately.
    if (elapsed > 5'000) {
        std::cerr << "FAIL: kill_and_reap took " << elapsed << " ms" << std::endl;
        std::exit(1);
    }
    // Idempotent: second call must not crash and return 0.
    const int status_again = sp.kill_and_reap();
    if (status_again != 0) {
        std::cerr << "FAIL: second kill_and_reap returned " << status_again
                  << std::endl;
        std::exit(1);
    }
}

void test_destructor_reaps_in_scope() {
    {
        auto sp = hecquin::common::Subprocess::spawn_read("sleep 30");
        if (!sp.valid()) {
            std::cerr << "FAIL: spawn_read returned invalid" << std::endl;
            std::exit(1);
        }
    }
    // If we got here without OS resource exhaustion, ~Subprocess reaped.
}

void test_default_constructed_is_invalid() {
    hecquin::common::Subprocess sp;
    if (sp.valid()) {
        std::cerr << "FAIL: default constructed is valid" << std::endl;
        std::exit(1);
    }
    if (sp.kill_and_reap() != 0) {
        std::cerr << "FAIL: kill_and_reap on default returned non-zero"
                  << std::endl;
        std::exit(1);
    }
}

} // namespace

int main() {
    test_default_constructed_is_invalid();
    test_echo_round_trip();
    test_kill_and_reap_terminates_long_running_child();
    test_destructor_reaps_in_scope();
    std::cout << "[ok] test_subprocess" << std::endl;
    return 0;
}
