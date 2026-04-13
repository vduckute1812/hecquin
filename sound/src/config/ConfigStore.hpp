#pragma once

#include <string>
#include <unordered_map>

/**
 * Key/value pairs from a dotenv-style file, merged with the process environment on lookup
 * (non-empty environment variables override file entries).
 */
class ConfigStore {
public:
    static constexpr const char* kDefaultPath = ".env/config.env";

    static ConfigStore from_path(const char* path);
    static ConfigStore load_default() { return from_path(kDefaultPath); }

    [[nodiscard]] std::string resolve(const std::string& key) const;

private:
    explicit ConfigStore(std::unordered_map<std::string, std::string> file_vars);

    std::unordered_map<std::string, std::string> file_vars_;
};
