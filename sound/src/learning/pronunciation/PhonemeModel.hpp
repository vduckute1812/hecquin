#pragma once

#include "learning/pronunciation/PhonemeTypes.hpp"
#include "learning/pronunciation/PhonemeVocab.hpp"

#include <memory>
#include <string>
#include <vector>

namespace hecquin::learning::pronunciation {

struct PhonemeModelConfig {
    std::string model_path;            ///< `.onnx` path on disk
    std::string vocab_path;            ///< HuggingFace `vocab.json`
    int sample_rate_hz = 16000;        ///< Wav2Vec2 is trained at 16 kHz
    float frame_stride_ms = 20.0f;     ///< stride reported to downstream scorers
    std::string provider = "cpu";      ///< "cpu" | "coreml" | "xnnpack"
};

/**
 * Thin wrapper around an onnxruntime session for a wav2vec2-phoneme CTC model.
 *
 * When the build is compiled without HECQUIN_WITH_ONNX, every call returns
 * `available() == false` / empty emissions, so callers can still be wired up
 * and degrade to an "unavailable" path at runtime — same treatment
 * `AiClientConfig` gives libcurl.
 */
class PhonemeModel {
public:
    PhonemeModel();
    ~PhonemeModel();

    PhonemeModel(const PhonemeModel&) = delete;
    PhonemeModel& operator=(const PhonemeModel&) = delete;

    /** Load model + vocab from disk.  Returns false on any failure. */
    bool load(const PhonemeModelConfig& cfg);

    /** True once `load()` succeeded. */
    [[nodiscard]] bool available() const;

    [[nodiscard]] const PhonemeVocab& vocab() const { return vocab_; }
    [[nodiscard]] const PhonemeModelConfig& config() const { return cfg_; }

    /**
     * Run inference on a float32 mono PCM buffer sampled at `cfg.sample_rate_hz`.
     * Returns the emission matrix (log-softmax of the model logits).  Empty
     * result on any failure.
     */
    [[nodiscard]] Emissions infer(const std::vector<float>& pcm) const;

    /**
     * Test-only hook: feed an emissions matrix directly.  Used by unit tests
     * so they do not need an onnx model on disk.
     */
    void set_fake_emissions_for_test(Emissions e) { fake_.emissions = std::move(e); fake_.enabled = true; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    PhonemeVocab vocab_;
    PhonemeModelConfig cfg_;

    struct Fake {
        bool enabled = false;
        Emissions emissions;
    } fake_;
};

} // namespace hecquin::learning::pronunciation
