#pragma once

#include <functional>
#include <string>

/**
 * One internal pipeline event — wired to
 * `LearningStore::record_pipeline_event` when a store is attached.
 *
 * Split out of `voice/VoiceListener.hpp` so the few telemetry consumers
 * don't pull in the entire listener header (CommandProcessor, Whisper,
 * AudioCapture, BargeInController, …).  `VoiceListener.hpp` re-exports
 * this header so existing call sites keep compiling unchanged.
 *
 * Keeping the sink as `std::function` means the voice layer stays free
 * of any `learning/` dependency, mirroring `ApiCallSink`.
 */
struct PipelineEvent {
    std::string event;      ///< "vad_gate" | "whisper" | "tts" | "drill" | …
    std::string outcome;    ///< "ok" | "skipped" | "error"
    long duration_ms = 0;
    std::string attrs_json; ///< optional JSON attrs; empty = {}
};
using PipelineEventSink = std::function<void(const PipelineEvent&)>;
