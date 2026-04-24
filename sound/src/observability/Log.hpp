#pragma once

// Tiny structured logger.
//
// Design goals:
//   * Zero allocation on the hot path when the level is filtered out.
//   * Pretty one-line output by default; switchable to JSON Lines at runtime
//     via `HECQUIN_LOG_FORMAT=json` so the dashboard / log-scrapers can ingest
//     without regex gymnastics.
//   * No external dependency beyond <iostream>.
//
// Usage:
//     hecquin::log::info("vad_gate", "outcome=accepted rms={:.3f}", rms);
//     hecquin::log::kv("whisper", "transcribed", {
//         {"latency_ms", std::to_string(ms)},
//         {"no_speech",  std::to_string(p)},
//     });
//
// The pretty format looks like `2026-04-24T13:45:02Z INFO [vad_gate] message`.
// The JSON format is one object per line:
//     {"ts":"2026-04-24T13:45:02Z","level":"info","tag":"vad_gate","msg":"..."}.

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace hecquin::log {

enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3 };

namespace detail {

inline Level parse_level_env() {
    const char* raw = std::getenv("HECQUIN_LOG_LEVEL");
    if (!raw || *raw == '\0') return Level::Info;
    std::string s(raw);
    for (auto& c : s) c = static_cast<char>(std::tolower(c));
    if (s == "debug") return Level::Debug;
    if (s == "info")  return Level::Info;
    if (s == "warn" || s == "warning") return Level::Warn;
    if (s == "error" || s == "err")    return Level::Error;
    return Level::Info;
}

inline bool parse_json_env() {
    const char* raw = std::getenv("HECQUIN_LOG_FORMAT");
    if (!raw || *raw == '\0') return false;
    std::string s(raw);
    for (auto& c : s) c = static_cast<char>(std::tolower(c));
    return s == "json";
}

inline std::mutex& mu() {
    static std::mutex m;
    return m;
}

inline Level& active_level() {
    static Level l = parse_level_env();
    return l;
}

inline bool& json_mode() {
    static bool j = parse_json_env();
    return j;
}

inline std::ostream& stream_for(Level l) {
    return l >= Level::Warn ? std::cerr : std::cout;
}

inline const char* level_name(Level l) {
    switch (l) {
        case Level::Debug: return "debug";
        case Level::Info:  return "info";
        case Level::Warn:  return "warn";
        case Level::Error: return "error";
    }
    return "info";
}

inline std::string iso_timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return std::string(buf);
}

inline std::string escape_json_string(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char tmp[8];
                    std::snprintf(tmp, sizeof(tmp), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += tmp;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

inline void emit(Level l,
                 std::string_view tag,
                 std::string_view msg,
                 std::initializer_list<std::pair<std::string_view, std::string>> kvs = {}) {
    if (l < active_level()) return;
    std::lock_guard<std::mutex> lock(mu());
    std::ostream& os = stream_for(l);
    if (json_mode()) {
        os << "{\"ts\":"   << escape_json_string(iso_timestamp())
           << ",\"level\":" << escape_json_string(level_name(l))
           << ",\"tag\":"   << escape_json_string(tag)
           << ",\"msg\":"   << escape_json_string(msg);
        for (const auto& [k, v] : kvs) {
            os << "," << escape_json_string(k) << ":" << escape_json_string(v);
        }
        os << "}\n";
    } else {
        os << iso_timestamp() << ' '
           << std::setw(5) << std::left << level_name(l) << ' '
           << '[' << tag << "] " << msg;
        for (const auto& [k, v] : kvs) {
            os << ' ' << k << '=' << v;
        }
        os << std::endl;
    }
}

} // namespace detail

/** Runtime override: set before the first emission to take effect globally. */
inline void set_level(Level l) { detail::active_level() = l; }
inline void set_json(bool on)  { detail::json_mode() = on; }

inline void debug(std::string_view tag, std::string_view msg,
                  std::initializer_list<std::pair<std::string_view, std::string>> kvs = {}) {
    detail::emit(Level::Debug, tag, msg, kvs);
}
inline void info(std::string_view tag, std::string_view msg,
                 std::initializer_list<std::pair<std::string_view, std::string>> kvs = {}) {
    detail::emit(Level::Info, tag, msg, kvs);
}
inline void warn(std::string_view tag, std::string_view msg,
                 std::initializer_list<std::pair<std::string_view, std::string>> kvs = {}) {
    detail::emit(Level::Warn, tag, msg, kvs);
}
inline void error(std::string_view tag, std::string_view msg,
                  std::initializer_list<std::pair<std::string_view, std::string>> kvs = {}) {
    detail::emit(Level::Error, tag, msg, kvs);
}

} // namespace hecquin::log
