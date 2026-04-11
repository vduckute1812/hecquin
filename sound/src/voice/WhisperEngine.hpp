#pragma once

#include <memory>
#include <string>
#include <vector>

struct whisper_context;

namespace detail {
void whisper_context_deleter(whisper_context* ctx) noexcept;
}

/** RAII wrapper around a whisper.cpp model and greedy decode for one-shot utterances. */
class WhisperEngine {
public:
    explicit WhisperEngine(const char* model_path);
    ~WhisperEngine() = default;

    WhisperEngine(const WhisperEngine&) = delete;
    WhisperEngine& operator=(const WhisperEngine&) = delete;
    WhisperEngine(WhisperEngine&&) = delete;
    WhisperEngine& operator=(WhisperEngine&&) = delete;

    bool isLoaded() const { return static_cast<bool>(ctx_); }

    /** Run inference; prints segments to stdout; returns joined transcript. */
    std::string transcribe(const std::vector<float>& samples);

private:
    std::unique_ptr<whisper_context, void (*)(whisper_context*)> ctx_{nullptr, detail::whisper_context_deleter};
};
