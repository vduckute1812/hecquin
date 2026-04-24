#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct whisper_context;

namespace detail {
void whisper_context_deleter(whisper_context* ctx) noexcept;
}

/**
 * Tunables for `WhisperEngine`.  Every threshold that used to be a file-scope
 * magic number now lives here so it can be overridden via config or env vars
 * without rebuilding.
 *
 * Env-var overrides (all optional, space / comma-separated not applicable):
 *
 *   HECQUIN_WHISPER_LANGUAGE       ISO-639-1 code, e.g. "en", "vi", "fr"
 *                                  "auto" enables Whisper's own language ID.
 *   HECQUIN_WHISPER_THREADS        positive int; 0 = auto
 *                                  (max(2, hardware_concurrency()/2)).
 *   HECQUIN_WHISPER_BEAM_SIZE      0 = greedy, >0 = beam-search width.
 *   HECQUIN_WHISPER_NO_SPEECH      float in [0,1]; per-segment no-speech
 *                                  probability cutoff.
 *   HECQUIN_WHISPER_MIN_ALNUM      minimum alphanumeric chars in the
 *                                  stripped transcript.
 *   HECQUIN_WHISPER_SUPPRESS_SEGS  "1" to silence the `  > …` per-segment
 *                                  stdout line in production.
 */
struct WhisperConfig {
    /**
     * Max per-segment no-speech probability we tolerate before treating the
     * decoded output as Whisper hallucinating over silence / music / static.
     */
    float no_speech_prob_max = 0.6f;

    /**
     * Decoded text shorter than this (after trimming to alphanumerics) is
     * almost always a hallucination triggered by a brief noise spike.
     */
    std::size_t min_alnum_chars = 2;

    /**
     * 0 = resolve to `max(2, hardware_concurrency()/2)` at construction time.
     * Anything else is passed through verbatim.
     */
    int n_threads = 0;

    /** ISO-639-1 language code, or "auto" to run Whisper's LID.  Default English. */
    std::string language = "en";

    /** 0 = greedy decoding.  >0 enables beam search at the given width. */
    int beam_size = 0;

    /** Set to true to silence the per-segment stdout line in production deployments. */
    bool suppress_segment_print = false;

    /** Populate fields from `HECQUIN_WHISPER_*` env vars (see header comment). */
    void apply_env_overrides();
};

/** RAII wrapper around a whisper.cpp model and greedy / beam decode. */
class WhisperEngine {
public:
    explicit WhisperEngine(const char* model_path, WhisperConfig cfg = {});
    ~WhisperEngine() = default;

    WhisperEngine(const WhisperEngine&) = delete;
    WhisperEngine& operator=(const WhisperEngine&) = delete;
    WhisperEngine(WhisperEngine&&) = delete;
    WhisperEngine& operator=(WhisperEngine&&) = delete;

    bool isLoaded() const { return static_cast<bool>(ctx_); }

    /** Run inference; prints segments to stdout; returns joined transcript. */
    std::string transcribe(const std::vector<float>& samples);

    /** Last inference latency in milliseconds (0 when nothing has been transcribed yet). */
    long last_latency_ms() const { return last_latency_ms_; }

    /** Peek at the worst per-segment no-speech probability from the last call. */
    float last_no_speech_prob() const { return last_no_speech_prob_; }

    const WhisperConfig& config() const { return cfg_; }

private:
    std::unique_ptr<whisper_context, void (*)(whisper_context*)> ctx_{nullptr, detail::whisper_context_deleter};
    WhisperConfig cfg_;
    long last_latency_ms_ = 0;
    float last_no_speech_prob_ = 0.0f;
};
