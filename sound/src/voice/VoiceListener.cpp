#include "voice/VoiceListener.hpp"

#include "common/EnvParse.hpp"
#include "voice/ActionSideEffectRegistry.hpp"
#include "voice/PipelineTelemetry.hpp"
#include "voice/SecondaryVadGate.hpp"
#include "voice/ThinkingScheduler.hpp"
#include "voice/TtsResponsePlayer.hpp"
#include "voice/UtteranceCollector.hpp"
#include "voice/UtteranceRouter.hpp"

#include <cctype>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <utility>

// VoiceListenerConfig::apply_env_overrides moved to
// voice/VoiceListenerConfig.cpp.

namespace {

hecquin::voice::AudioBargeInController::Config make_barge_config() {
    hecquin::voice::AudioBargeInController::Config cfg;
    cfg.apply_env_overrides();
    return cfg;
}

} // namespace

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
      cfg_(cfg),
      barge_(make_barge_config()) {
    // Mirror the controller's TTS-active threshold boost into the
    // collector's per-frame VAD so the boost is consistent whichever
    // side reads it.  Single source of truth: the barge config.
    cfg_.tts_threshold_boost = barge_.tts_threshold_boost();

    collector_ = std::make_unique<hecquin::voice::UtteranceCollector>(
        capture_, cfg_, app_running_);
    player_ = std::make_unique<hecquin::voice::TtsResponsePlayer>(
        capture_, piper_model_path_, &barge_, collector_.get());
    telemetry_ = std::make_unique<hecquin::voice::PipelineTelemetry>();
    thinking_scheduler_ =
        std::make_unique<hecquin::voice::ThinkingScheduler>();
    music_fx_.set_collector(collector_.get());
    music_fx_.set_barge_controller(&barge_);

    // Earcons + WakeWordGate honour env overrides up-front so the
    // first utterance already obeys `HECQUIN_EARCONS` / `HECQUIN_WAKE_MODE`.
    earcons_.apply_env_overrides();
    wake_gate_.apply_env_overrides();
    music_fx_.apply_env_overrides();
    // Default mode indicator: tiny mode-tinted earcon on every change.
    mode_indicator_ = std::make_unique<hecquin::voice::ModeIndicator>(&earcons_);
    // Drill ready-gate: env override only, default true preserves the
    // pre-existing snappy auto-advance behaviour.
    {
        bool flag = true;
        if (hecquin::common::env::parse_bool("HECQUIN_DRILL_AUTO_ADVANCE", flag)) {
            drill_auto_advance_ = flag;
        }
    }

    // Edge-triggered: collector tells us when frame VAD flips.
    collector_->set_voice_state_callback(
        [this](bool voice) { barge_.on_voice_state_change(voice); });
    // Frame-cadence: drives the controller's hold-timer release.
    collector_->set_frame_callback(
        [this] {
            barge_.tick(
                hecquin::voice::AudioBargeInController::Clock::now());
        });
    collector_->set_continue_clamp_callback(
        [this](float raw_c, float applied_c, float start_t) {
            if (telemetry_) {
                telemetry_->emit_vad_continue_clamped(raw_c, applied_c, start_t);
            }
        });
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
    VoiceListener::VadGateDecision out;
    out.accept = d.accept;
    out.too_quiet = d.too_quiet;
    out.too_sparse = d.too_sparse;
    out.too_flat = d.too_flat;
    out.mean_rms = d.mean_rms;
    out.voiced_ratio = d.voiced_ratio;
    out.crest_factor = d.crest_factor;
    return out;
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
    const ListenerMode prev_mode = mode_;
    switch (d.mode_change) {
        case MC::None:           break;
        case MC::Enter:          mode_ = d.mode_value; break;
        case MC::ExitTo:         mode_ = exit_target(d.mode_value); break;
        case MC::EnterIfEnable:
            mode_ = local.enable ? d.mode_value
                                 : exit_target(d.mode_value);
            break;
        case MC::ExitHome:
            // Wake / abort-style: always drop straight to home.  If
            // home is the mode we're "leaving", fall through to
            // Assistant so the user isn't stuck in a no-op loop.
            mode_ = home_mode_ == d.mode_value
                        ? ListenerMode::Assistant : home_mode_;
            break;
    }

    if (d.sets_pending_drill_from_enable) {
        pending_drill_announce_ =
            local.enable && static_cast<bool>(drill_announce_cb_);
    }

    if (d.music_hook) {
        (music_fx_.*d.music_hook)();
    }

    if (d.aborts_tts) {
        // Universal-stop: interrupt the assistant's reply immediately
        // so the user doesn't have to wait for Piper to finish what it
        // started.  Earcons for auditory acknowledgement live on the
        // listener (see `handle_routed_`).
        barge_.abort_tts_now();
        // Drop any pending follow-on so a "stop" mid-drill doesn't
        // immediately re-announce the next sentence.
        pending_drill_announce_ = false;
    }

    if (mode_ != prev_mode && mode_indicator_) {
        mode_indicator_->notify(mode_);
    }

    // Tier-4 #16: forward IdentifyUser to the upsert callback so the
    // learning store can mint / look up the row before any subsequent
    // progress write happens.  Empty `param` (matcher didn't capture a
    // name) is silently ignored — the spoken "Got it." reply still goes
    // out so the user knows the system heard them.
    if (local.kind == ActionKind::IdentifyUser &&
        user_identified_cb_ && !local.param.empty()) {
        user_identified_cb_(local.param);
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
    if (gate.too_flat) {
        std::cout << "🔇 Skipping noise (too_flat: crest_factor="
                  << gate.crest_factor << " < "
                  << cfg_.min_crest_factor << ")" << std::endl;
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
        effective_min_utt_rms, cfg_.min_voiced_frame_ratio,
        cfg_.min_crest_factor);

    if (gate.accept) return true;

    log_vad_rejection_(gate);
    if (telemetry_) telemetry_->emit_vad_rejection(gate, utt.speech_ms);
    earcons_.play_async(hecquin::voice::Earcons::Cue::VadRejected);
    capture_.clearBuffer();
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.poll_interval_ms));
    return false;
}

std::string VoiceListener::transcribe_and_emit_(
    const hecquin::voice::CollectedUtterance& utt) {
    std::string transcript = whisper_.transcribe(utt.pcm);

    // Post-Whisper strict cross-check: the basic post-filter accepts
    // any transcript whose alnum count >= cfg.min_alnum_chars and whose
    // worst no_speech_prob <= cfg.no_speech_prob_max.  That still lets
    // through borderline hallucinations like "Our bill takes" where the
    // text is short AND the no_speech_prob landed in the upper half of
    // the band.  Drop those before they burn a cloud LLM call.
    if (!transcript.empty() && cfg_.post_whisper_strict_min_alnum > 0) {
        std::size_t alnum = 0;
        for (char c : transcript) {
            if (std::isalnum(static_cast<unsigned char>(c))) ++alnum;
        }
        const bool short_alnum =
            static_cast<int>(alnum) < cfg_.post_whisper_strict_min_alnum;
        const bool high_no_speech =
            whisper_.last_no_speech_prob() >=
            cfg_.post_whisper_strict_no_speech_prob;
        if (short_alnum && high_no_speech) {
            std::cerr << "🔇 Dropping borderline noise transcript "
                         "(alnum=" << alnum << " < "
                      << cfg_.post_whisper_strict_min_alnum
                      << ", no_speech="
                      << whisper_.last_no_speech_prob()
                      << " >= "
                      << cfg_.post_whisper_strict_no_speech_prob
                      << "): '" << transcript << "'" << std::endl;
            transcript.clear();
        }
    }

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

    // Confirm-cancel: when the music side-effect arms a confirmation
    // window instead of aborting, override the spoken reply so the
    // user knows we're asking before we destroy.
    Action speak = action;
    if (action.kind == ActionKind::MusicCancel &&
        music_fx_.cancel_is_pending_confirm()) {
        speak.reply = "Stop the music?";
    }
    player_->speak(speak, current_mode_label_());
}

void VoiceListener::maybe_announce_drill_(ActionKind action_kind) {
    // After a scored drill attempt, queue an announcement of the next
    // target sentence so the session flows without needing a new wake
    // phrase between attempts.
    if (action_kind == ActionKind::PronunciationFeedback &&
        mode_ == ListenerMode::Drill && drill_announce_cb_) {
        pending_drill_announce_ = true;
    }
    // Explicit pacing: a `DrillAdvance` intent ("next"/"again"/"skip")
    // forces the announce flush regardless of the auto-advance flag,
    // so the user can advance even if they hadn't scored yet.
    if (action_kind == ActionKind::DrillAdvance &&
        mode_ == ListenerMode::Drill && drill_announce_cb_) {
        pending_drill_announce_ = true;
    }
    // Ready-gate: when auto-advance is off, only flush on explicit
    // user pacing (DrillAdvance) — `PronunciationFeedback` alone has
    // queued the announce but waits for the user's signal.
    const bool may_flush =
        drill_auto_advance_ || action_kind == ActionKind::DrillAdvance;
    if (pending_drill_announce_ && drill_announce_cb_ && may_flush) {
        pending_drill_announce_ = false;
        // The announce callback synthesises + plays the target
        // sentence; the same MuteGuard guards the mic so we don't
        // immediately re-detect our own speaker output.
        AudioCapture::MuteGuard mute(capture_);
        drill_announce_cb_();
    }
}

void VoiceListener::route_gated_utterance_(
    hecquin::voice::CollectedUtterance& utt,
    hecquin::voice::UtteranceRouter& router,
    const hecquin::voice::WakeWordGate::Decision& decision) {
    // Thinking earcon fires after cfg_.thinking_earcon_delay_ms so fast
    // local routes skip it; slow LLM paths get a soft pulse. Scheduler
    // is reused across turns (no per-utterance thread).
    const int thinking_delay_ms = cfg_.thinking_earcon_delay_ms;
    if (thinking_delay_ms > 0) {
        thinking_scheduler_->arm(
            std::chrono::milliseconds(thinking_delay_ms),
            [this] { earcons_.start_thinking(); });
    }

    const auto routed =
        router.route(Utterance{decision.transcript, utt.pcm});

    if (thinking_delay_ms > 0) {
        thinking_scheduler_->cancel();
        earcons_.stop_thinking(); // idempotent if start never fired
    }

    handle_routed_(routed.action);
    maybe_announce_drill_(routed.action.kind);
    std::cout << std::endl;
}

void VoiceListener::process_utterance_(
    hecquin::voice::CollectedUtterance& utt,
    hecquin::voice::UtteranceRouter& router) {
    if (!gate_accepts_(utt)) return;

    const std::string transcript = transcribe_and_emit_(utt);
    if (!transcript.empty()) {
        const auto d = wake_gate_.decide(transcript);

        if (mode_ == ListenerMode::Asleep) {
            if (d.route) {
                Action wake{};
                wake.kind = ActionKind::Wake;
                wake.reply = "I'm here.";
                wake.transcript = d.transcript;
                handle_routed_(wake);
            } else {
                std::cout << "😴 (sleeping; ignored)\n" << std::endl;
            }
        } else if (d.route) {
            route_gated_utterance_(utt, router, d);
        } else {
            std::cout << "🤫 (no wake phrase; ignored)\n" << std::endl;
        }
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
