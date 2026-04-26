#pragma once

#include <string>
#include <vector>

struct AppConfig;

namespace hecquin::voice {

/**
 * Boot-time capability check.  Inspects the config + linked deps and
 * builds a single human-readable line the assistant can speak after
 * the listener finishes calibrating.  Suppressible via
 * `HECQUIN_QUIET_BOOT=1` for production / installation Pi builds.
 *
 * Currently reports on three subsystems:
 *
 *   - Cloud assistant (`AiClientConfig::ready()` — endpoint + API key
 *     present; we do not actively probe the network here).
 *   - Pronunciation scorer (presence of the wav2vec2 ONNX model file).
 *   - Music provider (`yt-dlp` binary present; we don't run it).
 *
 * The report intentionally lists what's *unavailable* first — silence
 * about a feature is read as "available" and keeps the spoken line
 * short.
 */
struct CapabilityStatus {
    bool cloud_ready = true;
    bool pronunciation_ready = true;
    bool music_ready = true;

    /** When `quiet_boot` is true, callers should skip speaking. */
    bool quiet_boot = false;

    /** Build a one-sentence spoken summary suitable for TTS.  Returns
     *  an empty string when everything is ready (no need to disclose). */
    [[nodiscard]] std::string spoken_summary() const;
};

/** Probe a config for available subsystems.  Pure / cheap. */
[[nodiscard]] CapabilityStatus probe_capabilities(const AppConfig& cfg);

} // namespace hecquin::voice
