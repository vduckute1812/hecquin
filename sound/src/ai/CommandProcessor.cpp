#include "ai/CommandProcessor.hpp"

#include "actions/NoneAction.hpp"
#include "common/StringUtils.hpp"

#include <future>

using hecquin::ai::CurlHttpClient;
using hecquin::common::trim_copy;

CommandProcessor::CommandProcessor(AiClientConfig config)
    : config_(std::move(config)),
      owned_http_(std::make_unique<CurlHttpClient>()),
      http_(*owned_http_),
      chat_(config_, http_) {}

CommandProcessor::CommandProcessor(AiClientConfig config, hecquin::ai::IHttpClient& http)
    : config_(std::move(config)),
      owned_http_(nullptr),
      http_(http),
      chat_(config_, http_) {}

std::optional<Action> CommandProcessor::match_local(const std::string& transcript) const {
    return matcher_.match(transcript);
}

Action CommandProcessor::process(const std::string& transcript) {
    const std::string trimmed = trim_copy(transcript);
    if (trimmed.empty()) {
        return NoneAction::empty_transcript();
    }
    if (auto local = matcher_.match(trimmed)) {
        return *local;
    }
    return chat_.ask(trimmed);
}

std::future<Action> CommandProcessor::process_async(const std::string& transcript) {
    // NOTE: captures `this`; callers are responsible for keeping the
    // processor alive until the returned future resolves.
    return std::async(std::launch::async,
                      [this, transcript]() { return process(transcript); });
}
