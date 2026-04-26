#pragma once

#include <atomic>
#include <string>

class WhisperEngine;

namespace hecquin::voice {

/**
 * Pre-warms Piper + Whisper on detached worker threads so the first
 * spoken reply does not pay the full `posix_spawn` + model-load tax.
 *
 * Both warm-ups are best-effort: any failure is logged and ignored,
 * because a failed warm-up just means the first real call pays the
 * cost it would have anyway.
 */
class PrewarmService {
public:
    PrewarmService() = default;

    /**
     * Skip the warm-up entirely — useful in tests / when the binary
     * runs on a host without Piper or whisper-ggml available.
     * `HECQUIN_PREWARM=0` flips this from the env.
     */
    void set_enabled(bool on) { enabled_.store(on, std::memory_order_release); }
    bool enabled() const { return enabled_.load(std::memory_order_acquire); }

    /** Apply `HECQUIN_PREWARM` (`0` disables). */
    void apply_env_overrides();

    /** Detached-thread Piper warm-up.  No-op when disabled. */
    void warm_piper(std::string model_path);

    /** Detached-thread Whisper warm-up.  No-op when disabled. */
    void warm_whisper(WhisperEngine& engine);

private:
    std::atomic<bool> enabled_{true};
};

} // namespace hecquin::voice
