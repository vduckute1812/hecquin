#include "voice/VoiceListener.hpp"

#include "common/EnvParse.hpp"
#include "voice/ActionSideEffectRegistry.hpp"
#include "voice/PipelineTelemetry.hpp"
#include "voice/SecondaryVadGate.hpp"
#include "voice/TtsResponsePlayer.hpp"
#include "voice/UtteranceCollector.hpp"
#include "voice/UtteranceRouter.hpp"

#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

void VoiceListenerConfig::apply_env_overrides() {
    namespace env = hecquin::common::env;
    float v = 0.0f;
    if (env::parse_float("HECQUIN_VAD_VOICE_RMS_THRESHOLD", v)) {
        voice_rms_threshold = v;
        voice_rms_threshold_pinned = true;
    }
    if (env::parse_float("HECQUIN_VAD_MIN_VOICED_RATIO", v)) min_voiced_frame_ratio = v;
    if (env::parse_float("HECQUIN_VAD_MIN_UTTERANCE_RMS", v)) {
        min_utterance_rms = v;
        min_utterance_rms_pinned = true;
    }

    if (env::parse_float("HECQUIN_VAD_K_START", v))    k_start = v;
    if (env::parse_float("HECQUIN_VAD_K_CONTINUE", v)) k_continue = v;
    if (env::parse_float("HECQUIN_VAD_K_UTT", v))      k_utt = v;
    if (env::parse_float("HECQUIN_VAD_MIN_START_THR", v)) adaptive_min_start_thr = v;

    int iv = 0;
    if (env::parse_int("HECQUIN_VAD_MAX_UTTERANCE_MS", iv)) max_utterance_ms = iv;

    bool flag = false;
    if (env::parse_bool("HECQUIN_VAD_AUTO", flag)) {
        auto_calibrate = flag;
        auto_adapt = flag;
    }
    if (env::parse_bool("HECQUIN_VAD_DEBUG", flag)) {
        debug = flag;
    }
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
    telemetry_ = std::make_unique<hecquin::voice::PipelineTelemetry>();
    music_fx_.set_collector(collector_.get());
}

VoiceListener::~VoiceListener() = default;

void VoiceListener::setPipelineEventSink(PipelineEventSink s) {
    event_sink_ = s;
    if (telemetry_) telemetry_->set_sink(std::move(s));
}

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
    if (mode_ == ListenerMode::Music) return "music";
    return "assistant";
}

void VoiceListener::apply_local_intent_side_effects_(const Action& local) {
    using MC = hecquin::voice::ActionSideEffectDescriptor::ModeChange;

    // Fall-back target when the user exits a temporary sub-mode.  If
    // their home is the very mode they're exiting, drop to Assistant
    // so they aren't stuck (e.g. in english_tutor, "exit lesson"
    // should actually exit lesson even though home is Lesson).
    const auto exit_target = [this](ListenerMode leaving) {
        return home_mode_ == leaving ? ListenerMode::Assistant : home_mode_;
    };

    const auto& d = hecquin::voice::descriptor_for(local.kind);
    switch (d.mode_change) {
        case MC::None:           break;
        case MC::Enter:          mode_ = d.mode_value; break;
        case MC::ExitTo:         mode_ = exit_target(d.mode_value); break;
        case MC::EnterIfEnable:
            mode_ = local.enable ? d.mode_value
                                 : exit_target(d.mode_value);
            break;
    }

    if (d.sets_pending_drill_from_enable) {
        pending_drill_announce_ =
            local.enable && static_cast<bool>(drill_announce_cb_);
    }

    if (d.music_hook) {
        (music_fx_.*d.music_hook)();
    }
}

void VoiceListener::log_vad_rejection_(
    const hecquin::voice::VadGateDecision& gate) const {
    if (gate.too_quiet) {
        std::cout << "🔇 Skipping noise (too_quiet: mean_rms="
                  << gate.mean_rms << " < "
                  << collector_->effective_min_utterance_rms()
                  << ")" << std::endl;
    }
    if (gate.too_sparse) {
        std::cout << "🔇 Skipping noise (too_sparse: voiced_ratio="
                  << gate.voiced_ratio << " < "
                  << cfg_.min_voiced_frame_ratio << ")" << std::endl;
    }
    std::cout << std::endl;
}

void VoiceListener::print_startup_banner_() const {
    std::cout << "\n🎤 Listening..." << std::endl;
    if (cfg_.auto_calibrate) {
        std::cout << "Auto-VAD on (k_start=" << cfg_.k_start
                  << ", k_continue=" << cfg_.k_continue
                  << ", k_utt=" << cfg_.k_utt
                  << ", adapt=" << (cfg_.auto_adapt ? "yes" : "no")
                  << "). Set HECQUIN_VAD_AUTO=0 to use static thresholds."
                  << std::endl;
        std::cout << "Calibrating noise floor for ~"
                  << cfg_.calibration_ms
                  << " ms — please stay quiet. Speak after the "
                  << "🎯 line." << std::endl;
    } else {
        std::cout << "Auto-VAD off (start_thr=" << cfg_.voice_rms_threshold
                  << ", min_utt_rms=" << cfg_.min_utterance_rms << ")."
                  << std::endl;
        std::cout << "Speak anytime!" << std::endl;
    }
    std::cout << "Press Ctrl+C to exit.\n" << std::endl;
}

bool VoiceListener::gate_accepts_(
    const hecquin::voice::CollectedUtterance& utt) {
    // The collector's dynamic min-utterance-rms keeps the secondary
    // gate in lockstep with the primary VAD (both auto-tune from the
    // same noise floor when `cfg_.auto_calibrate` is on).
    const float effective_min_utt_rms =
        collector_->effective_min_utterance_rms();
    const auto gate = hecquin::voice::evaluate_for_utterance(
        utt, cfg_.poll_interval_ms,
        effective_min_utt_rms, cfg_.min_voiced_frame_ratio);

    if (gate.accept) return true;

    log_vad_rejection_(gate);
    if (telemetry_) telemetry_->emit_vad_rejection(gate, utt.speech_ms);
    capture_.clearBuffer();
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.poll_interval_ms));
    return false;
}

std::string VoiceListener::transcribe_and_emit_(
    const hecquin::voice::CollectedUtterance& utt) {
    std::string transcript = whisper_.transcribe(utt.pcm);
    if (telemetry_) {
        telemetry_->emit_whisper(whisper_.last_latency_ms(),
                                 whisper_.last_no_speech_prob(),
                                 transcript.size(), utt.speech_ms,
                                 /*ok=*/!transcript.empty());
    }
    return transcript;
}

void VoiceListener::handle_routed_(const Action& action) {
    // Side effects run for any action that can flip listener state.
    // Lesson/Drill toggles always come from local intent; MusicPlayback
    // can also be emitted by music_cb_ at the end of a song, so we
    // intentionally do not gate on `from_local_intent` — the function
    // switches on `kind`.
    apply_local_intent_side_effects_(action);
    player_->speak(action, current_mode_label_());
}

void VoiceListener::maybe_announce_drill_(ActionKind action_kind) {
    // After a scored drill attempt, queue an announcement of the next
    // target sentence so the session flows without needing a new wake
    // phrase between attempts.
    if (action_kind == ActionKind::PronunciationFeedback &&
        mode_ == ListenerMode::Drill && drill_announce_cb_) {
        pending_drill_announce_ = true;
    }
    if (pending_drill_announce_ && drill_announce_cb_) {
        pending_drill_announce_ = false;
        // The announce callback synthesises + plays the target
        // sentence; the same MuteGuard guards the mic so we don't
        // immediately re-detect our own speaker output.
        AudioCapture::MuteGuard mute(capture_);
        drill_announce_cb_();
    }
}

void VoiceListener::process_utterance_(
    hecquin::voice::CollectedUtterance& utt,
    hecquin::voice::UtteranceRouter& router) {
    if (!gate_accepts_(utt)) return;

    const std::string transcript = transcribe_and_emit_(utt);
    if (!transcript.empty()) {
        Utterance utterance{transcript, utt.pcm};
        const auto routed = router.route(utterance);
        handle_routed_(routed.action);
        maybe_announce_drill_(routed.action.kind);
        std::cout << std::endl;
    }

    capture_.clearBuffer();
    std::cout << "🎤 Listening again...\n" << std::endl;
}

void VoiceListener::run() {
    print_startup_banner_();
    capture_.resumeDevice();

    hecquin::voice::UtteranceRouter router(
        commands_, mode_,
        /*drill_cb=*/drill_cb_,
        /*tutor_cb=*/tutor_cb_,
        /*music_cb=*/music_cb_);

    while (app_running_.load()) {
        auto maybe = collector_->collect_next();
        if (!maybe) break;
        process_utterance_(*maybe, router);
    }

    capture_.pauseDevice();
}
