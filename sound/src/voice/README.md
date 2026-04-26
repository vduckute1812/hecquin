# `voice/`

Microphone capture, VAD, Whisper wrapper, and the listener orchestrator.
`VoiceListener` is a thin coordinator — most responsibilities live in
single-purpose collaborators under this same folder.

## Files

| File | Purpose |
|---|---|
| `AudioCapture.hpp/cpp` | SDL2 microphone capture: ring buffer, `snapshotRecent` (zero-alloc tail copy for VAD), `snapshotBuffer` (full copy on utterance close), `MuteGuard` RAII pause/resume wrapper used during TTS. |
| `AudioCaptureConfig.hpp` | POD config (no SDL include) so the rest of the codebase can reason about sample rate / channels without pulling in SDL. |
| `WhisperEngine.hpp/cpp` | RAII wrapper around `whisper_context`. Env-driven `WhisperConfig` (language / threads / beam / no-speech / min-alnum / suppress-segs). Inference + segment join only — gates live in `WhisperPostFilter`. `transcribe()` is a thin orchestrator that delegates parameter assembly to `build_wparams(WhisperConfig)` and segment iteration / no-speech tracking to `collect_segments(ctx, WhisperConfig)`. |
| `VoiceListenerConfig.hpp/cpp` | POD `VoiceListenerConfig` + `apply_env_overrides()` (lifted out of `VoiceListener.hpp`/`.cpp`). Lets a future config consumer pull just the listener's tuning surface without compiling the whole listener header. |
| `PipelineEvent.hpp` | `PipelineEvent` struct + `PipelineEventSink` typedef (lifted out of `VoiceListener.hpp`). Telemetry consumers can include this without the rest of the listener machinery. |
| `WhisperPostFilter.hpp/cpp` | Pure `filter(joined_text, worst_no_speech_prob, WhisperConfig) → optional<string>`. Strips bracketed / parenthetical non-speech annotations, enforces `min_alnum_chars`, applies the `no_speech_prob_max` gate. Unit-tested without a GGML model. |
| `ListenerMode.hpp` | Tiny header that owns the `ListenerMode { Assistant, Lesson, Drill, Music, Asleep }` enum. Lives on its own so `ActionSideEffectRegistry` and `VoiceListener` can both include it without circular deps. `Asleep` is entered on `Sleep` intent and left only via the wake gate (`Wake` intent / wake phrase / PTT press). |
| `VoiceListener.hpp/cpp` | Coordinator: poll loop + `ListenerMode` state machine + `PipelineEventSink` wiring + UX cue wiring (`Earcons`, `WakeWordGate`, `ModeIndicator`). Delegates to the collaborators below. `apply_local_intent_side_effects_` is now a 10-line dispatch over `ActionSideEffectRegistry::descriptor_for`; honours `aborts_tts` (universal stop), `ExitHome` mode-change (Wake), and forwards `IdentifyUser` to `user_identified_cb_`. |
| `ActionSideEffectRegistry.hpp/cpp` | Static `ActionKind → {ModeChange, ListenerMode target, music_hook, sets_pending_drill, aborts_tts}` table. Adding a new music / lesson / wake intent is a one-row extension instead of an N-case `switch`. New rows for `Sleep`, `Wake`, `AbortReply`, `MusicVolumeUp/Down`, `MusicSkip`. |
| `MusicWiring.hpp/cpp` | `install_music_wiring(VoiceListener&, MusicConfig) → MusicWiring` builds a `MusicProvider` via `MusicFactory`, wraps it in a `MusicSession`, and registers all six mid-song callbacks (handle / abort / pause / resume / volume / skip). Replaces the 15-line copy that used to live verbatim in every voice main. |
| `UtteranceCollector.hpp/cpp` | Owns the 50 ms loop, primary VAD counters, collection timers, and the adaptive noise-floor wiring (calibration + hysteresis). `collect_next()` is now a thin loop over `poll_tick_(...)` which packages each tick's work — VAD probe, floor update, debug log, advance — into a `TickResult { Continue, EmitUtterance }`. Snapshots the buffer only when the result is `EmitUtterance`. |
| `MusicSideEffects.hpp/cpp` | Façade that owns abort / pause / resume / volume / skip callbacks plus a non-owning `UtteranceCollector*` + `AudioBargeInController*` and routes every music intent (`MusicPlayback` / `MusicNotFound` / `MusicCancel` / `MusicPause` / `MusicResume` / `MusicVolumeUp/Down` / `MusicSkip`) without leaking music plumbing into the listener's main switch. The repeating `(if collector_) ... (if barge_) ...` shape collapses into a private `mark_external_audio_(bool)` helper. Two extras: `on_tts_speak_begin/end` hooks (TTS-ducks-music gain ramp via `barge_->tts_speak_*`) and an opt-in confirm-cancel state machine (`HECQUIN_CONFIRM_CANCEL=1`) that arms a duck + window on the first `MusicCancel` and only aborts on the second within the window. |
| `Earcons.hpp/cpp` | Tone bank + `+800 ms` thinking-pulse scheduler. Cues: `StartListening`, `VadRejected`, `Thinking`, `NetworkOffline`, `Acknowledge`, `Sleep`, `Wake`. Synthesised in-process (no `.wav` ships); optional WAV overrides via `set_search_dir(HECQUIN_EARCONS_DIR)`. `play_async` for non-blocking dispatch; `start_thinking / stop_thinking` for the LLM-tail latency mask. Disabled by `HECQUIN_EARCONS=0`. |
| `WakeWordGate.hpp/cpp` | Three-mode routing decision applied **after** Whisper but **before** the router. `always` (default — legacy behaviour), `wake_word` (transcripts must start with — or arrive within `HECQUIN_WAKE_WINDOW_MS` of — `HECQUIN_WAKE_PHRASE`; the phrase is stripped before dispatch), `ptt` (push-to-talk — only routes while a hardware GPIO pin / scripted `set_ptt_pressed` is held). The gate is also consulted while `mode_ == Asleep` so the wake intent / phrase / PTT press is the only way out. |
| `ModeIndicator.hpp/cpp` | Fired on every `mode_` change. Default impl plays a mode-tinted `Earcons::StartListening` variant; downstream GPIO / LED implementations subclass and override `notify(ListenerMode)`. `VoiceListener::set_mode_indicator(...)` swaps the implementation behind the same call site. |
| `AudioBargeInController.hpp/cpp` | Owns the music-gain ramp and the in-flight TTS abort fuse. `tts_speak_begin / end(gain, ramp_ms)` ramps music gain down while Piper speaks; `abort_tts_now()` is fired by the listener on `AbortReply` (ActionSideEffectRegistry's `aborts_tts=true`). |
| `NoiseFloorTracker.hpp/cpp` | Pure ambient-RMS tracker: median-based startup calibration + EMA over idle frames. Drives the adaptive thresholds the collector and secondary gate consume. No I/O, trivially testable. |
| `SecondaryVadGate.hpp/cpp` | Pure `evaluate_secondary_gate(...)` plus a convenience `evaluate_for_utterance(CollectedUtterance, ...)` that derives `effective_frames` and the utterance mean RMS. No I/O, trivially testable. |
| `TtsResponsePlayer.hpp/cpp` | TTS sanitisation regex set + Piper playback call. The two distinct lifecycles — `MuteGuard`-wrapped legacy path vs live-mic barge-in path — live in `speak_with_muted_mic_` / `speak_with_barge_in_`; `speak()` is a thin Strategy dispatcher (sanitise + label-print, then route on `barge_in_live_mic`). |
| `UtteranceRouter.hpp/cpp` | Chain of Responsibility: local intents → drill callback → tutor callback → `CommandProcessor::process` fallback. |
| `PipelineTelemetry.hpp/cpp` | Owns the optional `PipelineEventSink` and the JSON-attribute formatting for every per-stage event the listener emits (`vad_gate`, `whisper`). Centralises the schema. |
| `VoiceApp.hpp/cpp` | Shared bootstrap for voice executables (`config` → `AudioCapture` → `WhisperEngine` → `VoiceListener`). |
| `VoiceDetector.cpp` | Entry point for the `voice_detector` binary. |

`VoiceListenerConfig` and `WhisperConfig` read their `HECQUIN_*` env-var
overrides through the shared header-only helper at
[`../common/EnvParse.hpp`](../common/EnvParse.hpp) (`hecquin::common::env`).

## Listen loop (high level)

```
every 50 ms:
  UtteranceCollector.tick() → maybe CollectedUtterance
  on close:
    decision = SecondaryVadGate.evaluate(...)
    if not decision.accepted: skip, emit pipeline_event
    else:
      transcript = WhisperEngine.transcribe(samples)
      result     = UtteranceRouter.route({transcript, pcm})
      TtsResponsePlayer.speak(result.action.reply)
      update ListenerMode from result
```

Full pseudocode + the `VoiceListenerConfig` table is in
[`../../ARCHITECTURE.md#voicelistener`](../../ARCHITECTURE.md#voicelistener).

## Tests

- `tests/voice/test_voice_listener_vad.cpp` — secondary VAD gate reasons.
- `tests/voice/test_noise_floor_tracker.cpp` — calibration median, EMA convergence, speech rejection, clamps.
- `tests/voice/test_music_side_effects.cpp` — listener-side music callback dispatch + confirm-cancel state machine + TTS-ducks-music hooks. No SDL / Whisper.
- `tests/voice/test_whisper_post_filter.cpp` — annotation strip, min-alnum, no-speech probability gate. No GGML.
- `tests/voice/test_utterance_router.cpp` — Chain of Responsibility ordering, including `WakeWordGate` skip + abort-TTS side effect.

UX-layer cues / gates and their interaction with the listener are
described as a single narrative in
[`../../UX_FLOW.md`](../../UX_FLOW.md); keep changes to `Earcons`,
`WakeWordGate`, `ModeIndicator`, or `MusicSideEffects` synchronised
with that document.

## Adaptive VAD

`UtteranceCollector` measures the ambient RMS noise floor at startup
(median of the first ~1 s of idle frames) and continuously refines it
with an EMA over later idle frames.  All three RMS thresholds are then
derived from the floor `N`:

| Threshold | Formula | Multiplier env |
|---|---|---|
| Start (enter `Recording`) | `clamp(k_start * N)` | `HECQUIN_VAD_K_START` (default `3.0`) |
| Continue (stay in `Recording`) | `k_continue * start` | `HECQUIN_VAD_K_CONTINUE` (default `0.6`) |
| Secondary gate `min_utterance_rms` | `clamp(k_utt * N)` | `HECQUIN_VAD_K_UTT` (default `2.0`) |

The continue threshold is intentionally lower than the start threshold
(hysteresis): once an utterance has begun, quiet syllables and breaths
shouldn't end it prematurely.

### Boot-time handshake

Two safeguards keep the very first poll cycle from sabotaging the run:

1. `UtteranceCollector` refuses to start a new utterance while the
   calibration window is open, so mic warm-up noise can't false-trigger
   `🔴 Recording...` before the median floor is even available — and
   the calibration samples stay speech-free.
2. When calibration finishes, the collector prints a one-shot
   `🎯 Calibrated (floor=… start=… cont=… utt=…)` line.  That handshake
   is the user's signal that it's safe to speak; before it appears,
   `auto_calibrate` is still measuring the room.

If `auto_calibrate` is off, the gate is open immediately and the static
banner just says `Speak anytime!`.

### Safety net

`max_utterance_ms` (default 15 000 ms, env `HECQUIN_VAD_MAX_UTTERANCE_MS`,
`0` disables) force-closes a recording when speech_ms hits the cap even
if the silence timer never fires.  The collector emits a distinct
`⏹ Recording complete (max duration … ms reached)` line so the cause is
obvious — typically the continue threshold is chasing ambient noise
because the floor calibrated too low; raise `adaptive_min_start_thr`
(env `HECQUIN_VAD_MIN_START_THR`, default `0.005`) until the
hysteresis gap clears the room's noise peaks.

### Escape hatches

- `HECQUIN_VAD_AUTO=0` — disable auto-tuning entirely; use static
  `voice_rms_threshold` / `min_utterance_rms`.
- `HECQUIN_VAD_VOICE_RMS_THRESHOLD=…` / `HECQUIN_VAD_MIN_UTTERANCE_RMS=…`
  — pin a single field; the other(s) keep auto-tuning.
- `HECQUIN_VAD_MIN_START_THR=…` — raise the lower clamp on the
  start threshold (and therefore on the derived continue threshold)
  for noisy rooms / hot mics.
- `HECQUIN_VAD_MAX_UTTERANCE_MS=…` — change or disable the safety
  net.
- `HECQUIN_VAD_DEBUG=1` — log live floor + thresholds once per second
  while idle (`[vad] floor=… start=… cont=… utt=… rms=…`).

## Notes

- Mic echo is suppressed by the `MuteGuard` RAII wrapper, not by
  scattered `pauseDevice()` / `resumeDevice()` calls. Use the guard.
- Whisper noise / hallucination filters live inside `WhisperEngine`, not
  in callers — `transcribe()` returns an empty string when any gate trips.

## UML

### Class diagram — `VoiceListener` orchestrator + `UtteranceRouter` Chain of Responsibility + RAII guards

`VoiceListener` glues mic capture, VAD, Whisper, the router, and the TTS
player; `MuteGuard` and `WhisperEngine` are the RAII helpers that keep
the mic and the `whisper_context` lifetime honest.

```mermaid
classDiagram
    class VoiceApp {
        <<facade>>
        +init() bool
        +shutdown()
        +whisper()
        +capture()
    }
    class VoiceListener {
        <<orchestrator>>
        -mode_ : ListenerMode
        -home_mode_ : ListenerMode
        +run()
        +setTutorCallback(...)
        +setDrillCallback(...)
        +setMusicCallback(...)
    }
    class AudioCapture {
        +open(...)
        +pauseDevice()
        +resumeDevice()
        +clearBuffer()
        +snapshotBuffer()
        +snapshotRecent(n, out)
    }
    class MuteGuard {
        <<RAII>>
        +ctor(AudioCapture and)
        +dtor()
    }
    class WhisperEngine {
        <<RAII>>
        -ctx_ : whisper_context
        +transcribe(pcm) string
        +last_latency_ms()
    }
    class UtteranceCollector {
        +collect_next() optional_CollectedUtterance
    }
    class UtteranceRouter {
        <<ChainOfResponsibility>>
        +route(Utterance) Result
    }
    class TtsResponsePlayer {
        +speak(Action, mode_label)
    }
    class PipelineTelemetry {
        +emit_vad_rejection(VadGateDecision, speech_ms)
        +emit_whisper(latency_ms, p_no_speech, chars, speech_ms, ok)
        +set_sink(PipelineEventSink)
    }
    class CommandProcessor {
        +match_local(text)
        +process(text) Action
    }
    class ListenerMode {
        <<enum>>
        Assistant
        Lesson
        Drill
        Music
        Asleep
    }
    class Earcons {
        +play_async(Cue)
        +start_thinking()
        +stop_thinking()
    }
    class WakeWordGate {
        +decide(transcript) Decision
        +set_ptt_pressed(bool)
    }
    class ModeIndicator {
        <<base>>
        +notify(ListenerMode)
    }
    class AudioBargeInController {
        +tts_speak_begin(gain, ms)
        +tts_speak_end(ms)
        +abort_tts_now()
    }

    VoiceApp o-- WhisperEngine
    VoiceApp o-- AudioCapture
    VoiceListener o-- WhisperEngine
    VoiceListener o-- AudioCapture
    VoiceListener o-- UtteranceCollector
    VoiceListener o-- TtsResponsePlayer
    VoiceListener o-- PipelineTelemetry
    VoiceListener o-- CommandProcessor
    VoiceListener o-- Earcons
    VoiceListener o-- WakeWordGate
    VoiceListener o-- ModeIndicator
    VoiceListener o-- AudioBargeInController
    VoiceListener --> ListenerMode : mode_, home_mode_
    UtteranceCollector ..> AudioCapture : polls + RMS
    UtteranceRouter ..> CommandProcessor : match_local + process
    AudioCapture *-- MuteGuard : nested RAII
    TtsResponsePlayer ..> MuteGuard : mutes mic during TTS
```

### Sequence diagram — `VoiceListener::run` loop

One iteration of the listener loop: collect a candidate utterance, run
the secondary VAD gate, transcribe, route to an `Action`, apply side
effects (mode toggles, pending drill announce), and play the TTS reply
with the mic muted via `MuteGuard`. The router's branching reflects the
implementation in
[`UtteranceRouter.cpp`](./UtteranceRouter.cpp).

```mermaid
sequenceDiagram
    participant L as VoiceListener
    participant C as UtteranceCollector
    participant Cap as AudioCapture
    participant W as WhisperEngine
    participant R as UtteranceRouter
    participant Cmd as CommandProcessor
    participant T as TtsResponsePlayer

    L->>Cap: resumeDevice()
    loop while app_running
        L->>C: collect_next()
        C->>Cap: snapshotRecent / snapshotBuffer
        Cap-->>C: pcm
        C-->>L: CollectedUtterance
        L->>L: evaluate_secondary_gate(rms, voiced_ratio)
        alt rejected
            L->>Cap: clearBuffer()
        else accepted
            L->>W: transcribe(pcm)
            W-->>L: transcript
            L->>R: route(Utterance)
            R->>Cmd: match_local(transcript)
            alt local hit
                Cmd-->>R: Action
            else mode branch (Music / Drill / Lesson)
                R->>R: invoke mode callback
            else fallback
                R->>Cmd: process(transcript)
                Cmd-->>R: Action
            end
            R-->>L: Result with action + from_local_intent
            L->>L: apply_local_intent_side_effects(action)
            L->>T: speak(action, mode_label)
            T->>Cap: MuteGuard ctor pause + clear
            T->>T: piper_speak_and_play_streaming
            T->>Cap: MuteGuard dtor clear + resume
        end
    end
```

### State diagram — `ListenerMode`

`VoiceListener::apply_local_intent_side_effects_` mutates `mode_` in
response to selected `ActionKind` values; `home_mode_` controls where
`Music` returns to. There is no first-class FSM type — the diagram
models the observable transitions.

```mermaid
stateDiagram-v2
    [*] --> Assistant
    Assistant --> Lesson : LessonModeToggle enable
    Lesson --> Assistant : LessonModeToggle disable
    Assistant --> Drill : DrillModeToggle enable
    Drill --> Assistant : DrillModeToggle disable
    Lesson --> Drill : DrillModeToggle enable
    Drill --> Lesson : DrillModeToggle disable, home=Lesson
    Assistant --> Music : MusicSearchPrompt
    Lesson --> Music : MusicSearchPrompt
    Music --> Assistant : MusicPlayback / MusicNotFound / MusicCancel, home=Assistant
    Music --> Lesson : MusicPlayback / MusicNotFound / MusicCancel, home=Lesson
    Assistant --> Asleep : Sleep intent
    Lesson --> Asleep : Sleep intent
    Drill --> Asleep : Sleep intent
    Music --> Asleep : Sleep intent (after MusicCancel)
    Asleep --> Assistant : Wake intent / wake phrase / PTT, home=Assistant
    Asleep --> Lesson : Wake intent / wake phrase / PTT, home=Lesson
    Asleep --> Drill : Wake intent / wake phrase / PTT, home=Drill
```

The wake gate (`WakeWordGate`) is consulted on every transcript while
`mode_ == Asleep` so only a `Wake` intent, the configured wake phrase,
or a PTT press exits sleep. See
[`../../UX_FLOW.md#sleepwake-cycle`](../../UX_FLOW.md) for the
narrative + sequence diagram.

### State diagram — `UtteranceCollector`

Inside `UtteranceCollector::collect_next` an implicit boolean
`collecting` plus the trailing-RMS window form a small FSM that emits
a `CollectedUtterance` once the min-speech and end-silence thresholds
are met.

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Collecting : RMS above voice_rms_threshold
    Collecting --> Collecting : voiced frame, speech_ms grows
    Collecting --> Idle : silence before min_speech_ms
    Collecting --> EndOfUtterance : speech_ms ge min_speech_ms AND silence_ms ge end_silence_ms
    EndOfUtterance --> [*] : return CollectedUtterance
    Idle --> [*] : app_running cleared
```
