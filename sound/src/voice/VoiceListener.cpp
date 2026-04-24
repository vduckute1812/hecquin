#include "voice/VoiceListener.hpp"

#include "voice/SecondaryVadGate.hpp"
#include "voice/TtsResponsePlayer.hpp"
#include "voice/UtteranceCollector.hpp"
#include "voice/UtteranceRouter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <thread>
#include <utility>

namespace {

bool parse_float_env(const char* name, float& out) {
    const char* raw = std::getenv(name);
    if (!raw || *raw == '\0') return false;
    try {
        out = std::stof(raw);
        return true;
    } catch (...) {
        std::cerr << "[voice] ignoring invalid " << name << "=" << raw << std::endl;
        return false;
    }
}

} // namespace

void VoiceListenerConfig::apply_env_overrides() {
    float v = 0.0f;
    if (parse_float_env("HECQUIN_VAD_VOICE_RMS_THRESHOLD", v)) voice_rms_threshold = v;
    if (parse_float_env("HECQUIN_VAD_MIN_VOICED_RATIO", v))    min_voiced_frame_ratio = v;
    if (parse_float_env("HECQUIN_VAD_MIN_UTTERANCE_RMS", v))   min_utterance_rms = v;
}

VoiceListener::VoiceListener(WhisperEngine& whisper,
                             AudioCapture& capture,
                             CommandProcessor& commands,
                             std::atomic<bool>& app_running,
                             std::string piper_model_path,
                             VoiceListenerConfig cfg)
    : whisper_(whisper),
      capture_(capture),
      commands_(commands),
      app_running_(app_running),
      piper_model_path_(std::move(piper_model_path)),
      cfg_(cfg) {
    collector_ = std::make_unique<hecquin::voice::UtteranceCollector>(
        capture_, cfg_, app_running_);
    player_ = std::make_unique<hecquin::voice::TtsResponsePlayer>(
        capture_, piper_model_path_);
}

VoiceListener::~VoiceListener() = default;

VoiceListener::VadGateDecision VoiceListener::evaluate_secondary_gate(
    int voiced_frames, int effective_frames, float mean_rms,
    const VoiceListenerConfig& cfg) {
    const auto d = hecquin::voice::evaluate_secondary_gate(
        voiced_frames, effective_frames, mean_rms,
        cfg.min_utterance_rms, cfg.min_voiced_frame_ratio);
    return {d.accept, d.too_quiet, d.too_sparse, d.mean_rms, d.voiced_ratio};
}

const char* VoiceListener::current_mode_label_() const {
    if (mode_ == ListenerMode::Lesson) return "lesson";
    if (mode_ == ListenerMode::Drill) return "drill";
    return "assistant";
}

void VoiceListener::apply_local_intent_side_effects_(const Action& local) {
    // Mode we fall back to when the user exits a temporary sub-mode.
    // If their home is the very mode they're exiting, drop to Assistant
    // so they aren't stuck (e.g. in english_tutor, "exit lesson" should
    // actually exit lesson even though home is Lesson).
    auto exit_target = [this](ListenerMode leaving) {
        return home_mode_ == leaving ? ListenerMode::Assistant : home_mode_;
    };

    if (local.kind == ActionKind::LessonModeToggle) {
        mode_ = local.enable ? ListenerMode::Lesson
                             : exit_target(ListenerMode::Lesson);
    } else if (local.kind == ActionKind::DrillModeToggle) {
        mode_ = local.enable ? ListenerMode::Drill
                             : exit_target(ListenerMode::Drill);
        pending_drill_announce_ =
            local.enable && static_cast<bool>(drill_announce_cb_);
    }
}

void VoiceListener::handle_vad_rejection_(const hecquin::voice::VadGateDecision& gate,
                                          int speech_ms) {
    if (gate.too_quiet) {
        std::cout << "🔇 Skipping noise (too_quiet: mean_rms="
                  << gate.mean_rms << " < " << cfg_.min_utterance_rms
                  << ")" << std::endl;
    }
    if (gate.too_sparse) {
        std::cout << "🔇 Skipping noise (too_sparse: voiced_ratio="
                  << gate.voiced_ratio << " < "
                  << cfg_.min_voiced_frame_ratio << ")" << std::endl;
    }
    std::cout << std::endl;
    if (event_sink_) {
        // Tiny hand-rolled JSON — avoids pulling nlohmann into the
        // voice library just for a couple of attrs.
        std::ostringstream attrs;
        attrs << "{\"reason\":\""
              << (gate.too_quiet ? (gate.too_sparse ? "too_quiet+too_sparse" : "too_quiet")
                                 : "too_sparse")
              << "\",\"mean_rms\":" << gate.mean_rms
              << ",\"voiced_ratio\":" << gate.voiced_ratio
              << ",\"speech_ms\":" << speech_ms << "}";
        event_sink_({"vad_gate", "skipped", speech_ms, attrs.str()});
    }
}

void VoiceListener::run() {
    std::cout << "\n🎤 Listening... (Speak anytime!)" << std::endl;
    std::cout << "Press Ctrl+C to exit.\n" << std::endl;

    capture_.resumeDevice();

    hecquin::voice::UtteranceRouter router(
        commands_, mode_,
        /*drill_cb=*/drill_cb_,
        /*tutor_cb=*/tutor_cb_);

    while (app_running_.load()) {
        auto maybe = collector_->collect_next();
        if (!maybe) break;
        auto& utt = *maybe;

        // Secondary VAD gate: only hand the buffer to Whisper when the
        // utterance was sustainedly voiced and loud enough on average.
        // Subtract the terminal silence tail from the denominator so
        // short utterances aren't unfairly punished.
        const int tail_silence_frames =
            cfg_.poll_interval_ms > 0 ? utt.silence_ms / cfg_.poll_interval_ms : 0;
        const int effective_frames =
            std::max(1, utt.total_frames - tail_silence_frames);
        const float utt_rms =
            utt.pcm.empty()
                ? 0.0f
                : hecquin::voice::UtteranceCollector::rms(utt.pcm, 0, utt.pcm.size());
        const auto gate = hecquin::voice::evaluate_secondary_gate(
            utt.voiced_frames, effective_frames, utt_rms,
            cfg_.min_utterance_rms, cfg_.min_voiced_frame_ratio);

        if (!gate.accept) {
            handle_vad_rejection_(gate, utt.speech_ms);
            capture_.clearBuffer();
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.poll_interval_ms));
            continue;
        }

        const std::string transcript = whisper_.transcribe(utt.pcm);
        if (event_sink_) {
            std::ostringstream attrs;
            attrs << "{\"no_speech_prob\":" << whisper_.last_no_speech_prob()
                  << ",\"chars\":" << transcript.size()
                  << ",\"speech_ms\":" << utt.speech_ms << "}";
            event_sink_({"whisper",
                         transcript.empty() ? "skipped" : "ok",
                         whisper_.last_latency_ms(),
                         attrs.str()});
        }

        if (!transcript.empty()) {
            Utterance utterance{transcript, utt.pcm};
            const auto routed = router.route(utterance);
            if (routed.from_local_intent) {
                apply_local_intent_side_effects_(routed.action);
            }
            player_->speak(routed.action, current_mode_label_());

            // After a scored drill attempt, automatically announce the
            // next target sentence so the session flows without needing
            // a new wake phrase between attempts.
            if (routed.action.kind == ActionKind::PronunciationFeedback &&
                mode_ == ListenerMode::Drill && drill_announce_cb_) {
                pending_drill_announce_ = true;
            }
            if (pending_drill_announce_ && drill_announce_cb_) {
                pending_drill_announce_ = false;
                // The announce callback synthesises + plays the target
                // sentence; use the same MuteGuard so we don't
                // immediately re-detect our own speaker output.
                AudioCapture::MuteGuard mute(capture_);
                drill_announce_cb_();
            }
            std::cout << std::endl;
        }

        capture_.clearBuffer();
        std::cout << "🎤 Listening again...\n" << std::endl;
    }

    capture_.pauseDevice();
}
