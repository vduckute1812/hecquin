# `voice/`

Microphone capture, VAD, Whisper wrapper, and the listener orchestrator.
`VoiceListener` is a thin coordinator — most responsibilities live in
single-purpose collaborators under this same folder.

## Files

| File | Purpose |
|---|---|
| `AudioCapture.hpp/cpp` | SDL2 microphone capture: ring buffer, `snapshotRecent` (zero-alloc tail copy for VAD), `snapshotBuffer` (full copy on utterance close), `MuteGuard` RAII pause/resume wrapper used during TTS. |
| `AudioCaptureConfig.hpp` | POD config (no SDL include) so the rest of the codebase can reason about sample rate / channels without pulling in SDL. |
| `WhisperEngine.hpp/cpp` | RAII wrapper around `whisper_context`. Env-driven `WhisperConfig` (language / threads / beam / no-speech / min-alnum / suppress-segs). Three-layer noise + hallucination filter on decoder output. |
| `VoiceListener.hpp/cpp` | Coordinator: poll loop + `ListenerMode` state machine + `PipelineEventSink` wiring. Delegates to the four collaborators below. |
| `UtteranceCollector.hpp/cpp` | Owns the 50 ms loop, primary VAD counters, collection timers. Emits a `CollectedUtterance { pcm, stats }` when silence closes. |
| `SecondaryVadGate.hpp/cpp` | Pure `evaluate_secondary_gate(samples, voiced, total, cfg)` → accept / reject with reason. No I/O, trivially testable. |
| `TtsResponsePlayer.hpp/cpp` | TTS sanitisation regex set + `MuteGuard` wrap + Piper playback call. Removes Piper and regex from the listener. |
| `UtteranceRouter.hpp/cpp` | Chain of Responsibility: local intents → drill callback → tutor callback → `CommandProcessor::process` fallback. |
| `VoiceApp.hpp/cpp` | Shared bootstrap for voice executables (`config` → `AudioCapture` → `WhisperEngine` → `VoiceListener`). |
| `VoiceDetector.cpp` | Entry point for the `voice_detector` binary. |

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

- `tests/test_voice_listener_vad.cpp` — secondary VAD gate reasons.
- `tests/test_utterance_router.cpp` — Chain of Responsibility ordering.

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
    }

    VoiceApp o-- WhisperEngine
    VoiceApp o-- AudioCapture
    VoiceListener o-- WhisperEngine
    VoiceListener o-- AudioCapture
    VoiceListener o-- UtteranceCollector
    VoiceListener o-- TtsResponsePlayer
    VoiceListener o-- CommandProcessor
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
    Assistant --> Music : MusicSearchPrompt or MusicPlayback
    Lesson --> Music : MusicSearchPrompt
    Music --> Assistant : MusicAction cancel, home=Assistant
    Music --> Lesson : MusicAction cancel, home=Lesson
```

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
