#include "config/ConfigStore.hpp"

#include "common/StringUtils.hpp"

#include <cstdlib>
#include <fstream>

namespace {

using hecquin::common::starts_with;
using hecquin::common::trim_copy;

std::string getenv_string(const char* key) {
    const char* v = std::getenv(key);
    return v ? std::string(v) : std::string();
}

std::string strip_quotes(std::string s) {
    s = trim_copy(std::move(s));
    if (s.size() >= 2) {
        const char a = s.front();
        const char b = s.back();
        if ((a == '"' && b == '"') || (a == '\'' && b == '\'')) {
            s = s.substr(1, s.size() - 2);
        }
    }
    return s;
}

std::unordered_map<std::string, std::string> parse_env_file(const char* path) {
    std::unordered_map<std::string, std::string> out;
    std::ifstream in(path);
    if (!in) {
        return out;
    }
    std::string line;
    while (std::getline(in, line)) {
        line = trim_copy(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (starts_with(line, "export ")) {
            line = trim_copy(line.substr(7));
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = trim_copy(line.substr(0, eq));
        if (key.empty()) {
            continue;
        }
        std::string val = strip_quotes(trim_copy(line.substr(eq + 1)));
        out[std::move(key)] = std::move(val);
    }
    return out;
}

} // namespace

ConfigStore::ConfigStore(std::unordered_map<std::string, std::string> file_vars)
    : file_vars_(std::move(file_vars)) {}

ConfigStore ConfigStore::from_path(const char* path) {
    return ConfigStore(parse_env_file(path));
}

std::string ConfigStore::resolve(const std::string& key) const {
    std::string v = getenv_string(key.c_str());
    if (!v.empty()) {
        return v;
    }
    const auto it = file_vars_.find(key);
    if (it != file_vars_.end()) {
        return it->second;
    }
    return {};
}
