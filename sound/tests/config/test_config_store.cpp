#include "config/ConfigStore.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

int fail(const char* message) {
    std::cerr << "[test_config_store] FAIL: " << message << std::endl;
    return 1;
}

std::string write_tmp_env(const std::string& body) {
    char buf[] = "/tmp/hecquin_cfgstore_XXXXXX";
    const int fd = mkstemp(buf);
    if (fd < 0) return {};
    if (write(fd, body.data(), body.size()) != static_cast<ssize_t>(body.size())) {
        close(fd);
        return {};
    }
    close(fd);
    return buf;
}

} // namespace

int main() {
    const std::string body =
        "# comment at start\n"
        "FOO=bar\n"
        "QUOTED=\"hello world\"\n"
        "export EXPORTED='value with spaces'\n"
        "\n"
        "# inline comment line\n"
        "EMPTY=\n"
        "=skip_this\n"
        "WITH_EQ=key=val=pair\n";
    const std::string path = write_tmp_env(body);
    if (path.empty()) return fail("could not create tmp file");

    // Scrub env vars we will look up, so we actually hit file_vars_.
    unsetenv("FOO");
    unsetenv("QUOTED");
    unsetenv("EXPORTED");
    unsetenv("EMPTY");
    unsetenv("MISSING");
    unsetenv("WITH_EQ");

    const ConfigStore cfg = ConfigStore::from_path(path.c_str());

    if (cfg.resolve("FOO") != "bar") return fail("simple key=value");
    if (cfg.resolve("QUOTED") != "hello world") return fail("double-quoted value");
    if (cfg.resolve("EXPORTED") != "value with spaces") return fail("export + single-quoted");
    if (cfg.resolve("EMPTY") != "") return fail("empty value is empty string");
    if (cfg.resolve("MISSING") != "") return fail("unknown key returns empty");
    if (cfg.resolve("WITH_EQ") != "key=val=pair") return fail("value can contain '='");

    // Env override beats file value.
    setenv("FOO", "from_env", 1);
    if (cfg.resolve("FOO") != "from_env") return fail("env overrides file");
    unsetenv("FOO");

    std::remove(path.c_str());
    return 0;
}
