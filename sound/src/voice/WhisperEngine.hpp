#pragma once

#include <string>
#include <vector>

struct whisper_context;

/** RAII wrapper around a whisper.cpp model and greedy decode for one-shot utterances. */
class WhisperEngine {
public:
    explicit WhisperEngine(const char* model_path);
    ~WhisperEngine();

    WhisperEngine(const WhisperEngine&) = delete;
    WhisperEngine& operator=(const WhisperEngine&) = delete;
    WhisperEngine(WhisperEngine&&) = delete;
    WhisperEngine& operator=(WhisperEngine&&) = delete;

    bool isLoaded() const { return ctx_ != nullptr; }

    /** Run inference; prints segments to stdout; returns joined transcript. */
    std::string transcribe(const std::vector<float>& samples);

private:
    whisper_context* ctx_{nullptr};
};
