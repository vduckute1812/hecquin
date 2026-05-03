# UX diagrams (Mermaid)

Companion to [`UX_FLOW.md`](../UX_FLOW.md).  
All **state machines** and **UX-focused sequences** for the voice layer live here so the main doc stays scannable.
For **boot**, **full voice-turn choreography**, **TTS barge-in**, and **music session** flows across modules, see [`SEQUENCE_DIAGRAMS.md`](./SEQUENCE_DIAGRAMS.md).

---

## 5. Diagrams

### 5.1 Mode state machine (with `Asleep`)

```mermaid
stateDiagram-v2
    [*] --> Assistant : binary boot
    Assistant --> Lesson : LessonModeToggle (enable)
    Lesson --> Assistant : LessonModeToggle (disable)
    Assistant --> Drill : DrillModeToggle (enable)
    Drill --> Assistant : DrillModeToggle (disable)
    Drill --> Drill : DrillAdvance (next/again/skip)
    Assistant --> Music : MusicSearchPrompt
    Music --> Assistant : MusicPlayback / MusicNotFound (ExitTo)
    Music --> Assistant : MusicCancel (after confirm if armed)

    Assistant --> Asleep : Sleep
    Lesson --> Asleep : Sleep
    Drill --> Asleep : Sleep
    Music --> Asleep : Sleep
    Asleep --> Assistant : Wake (or wake phrase / PTT press)

    note right of Asleep
        Whisper still runs but only the wake intent
        (or HECQUIN_WAKE_MODE=ptt + GPIO press) routes.
        Everything else is silently dropped.
    end note
```

### 5.2 Per-utterance UX cue timeline (success path)

```mermaid
sequenceDiagram
    autonumber
    participant U as User
    participant Col as UtteranceCollector
    participant E as Earcons
    participant Wake as WakeWordGate
    participant W as WhisperEngine
    participant R as UtteranceRouter
    participant TTS as TtsResponsePlayer
    participant FX as MusicSideEffects

    U->>Col: speech (rms > threshold)
    Col->>E: play_async(StartListening)
    Col-->>Col: collect until end-silence
    Col->>W: pcm
    W-->>Col: transcript
    Col->>Wake: decide(transcript)
    Wake-->>Col: route + stripped transcript
    Col->>R: route({transcript, pcm})
    par +800 ms scheduler
        E-->>E: start_thinking() (only if route has not returned)
    end
    R-->>Col: action
    E->>E: stop_thinking()
    Col->>FX: on_tts_speak_begin() (duck music if any)
    Col->>TTS: speak(action)
    TTS-->>U: synthesised reply
    Col->>FX: on_tts_speak_end() (restore gain)
```

### 5.3 Per-utterance UX cue timeline (rejection / abort)

```mermaid
sequenceDiagram
    autonumber
    participant U as User
    participant Col as UtteranceCollector
    participant E as Earcons
    participant Gate as SecondaryVadGate
    participant Reg as ActionSideEffectRegistry
    participant Barge as AudioBargeInController

    rect rgb(245, 230, 230)
    Note over U,Gate: VAD rejection path
    U->>Col: speech (too quiet / too sparse)
    Col->>Gate: evaluate_secondary_gate
    Gate-->>Col: skip (too_quiet / too_sparse)
    Col->>E: play_async(VadRejected)
    Col-->>U: (no Whisper, no router, no TTS)
    end

    rect rgb(230, 245, 235)
    Note over U,Barge: Universal abort (mid-reply)
    U->>Col: "stop" / "shut up" / "never mind"
    Col->>Reg: descriptor_for(AbortReply)
    Reg-->>Col: aborts_tts = true
    Col->>Barge: abort_tts_now()
    Col->>E: play_async(Acknowledge)
    Note over Barge: in-flight Piper synth fuse fires;<br>StreamingSdlPlayer drains and stops
    end
```

### 5.4 Sleep / Wake cycle

```mermaid
sequenceDiagram
    autonumber
    participant U as User
    participant L as VoiceListener
    participant Loc as LocalIntentMatcher
    participant Reg as ActionSideEffectRegistry
    participant E as Earcons
    participant W as WakeWordGate

    U->>L: "go to sleep"
    L->>Loc: match_local
    Loc-->>L: Action{Sleep}
    L->>Reg: descriptor_for(Sleep)
    Reg-->>L: ModeChange::Enter Asleep
    L->>E: Cue::Sleep
    L-->>U: "Going to sleep."
    Note over L: mode_ = Asleep

    loop while Asleep
        U->>L: random utterance
        L->>W: WakeWordGate.decide()
        W-->>L: skip (no wake phrase / PTT)
        Note over L: silent drop
    end

    U->>L: "wake up" (or PTT press / wake phrase)
    L->>Loc: match_local
    Loc-->>L: Action{Wake}
    L->>Reg: descriptor_for(Wake)
    Reg-->>L: ModeChange::ExitHome
    Note over L: mode_ = home_mode_ (Assistant / Lesson / Drill)
    L->>E: Cue::Wake
    L-->>U: "I'm here."
```

### 5.5 Music confirm-cancel state machine

```mermaid
stateDiagram-v2
    [*] --> Idle : not playing
    Idle --> Playing : MusicPlayback
    Playing --> Playing : MusicVolumeUp/Down/Skip
    Playing --> Paused : MusicPause
    Paused --> Playing : MusicResume

    state confirm <<choice>>

    Playing --> confirm : MusicCancel
    confirm --> Cancelled : HECQUIN_CONFIRM_CANCEL=0
    confirm --> ConfirmArmed : HECQUIN_CONFIRM_CANCEL=1

    ConfirmArmed --> ConfirmArmed : MusicVolumeUp/Down<br>(window unaffected)
    ConfirmArmed --> Cancelled : 2nd MusicCancel<br>within window
    ConfirmArmed --> Playing : window expires<br>(duck released, song stays)

    Cancelled --> Idle : worker thread joins<br>noise floor reset
```

### 5.6 TTS-ducks-music gain timeline

```mermaid
sequenceDiagram
    autonumber
    participant TTS as TtsResponsePlayer
    participant FX as MusicSideEffects
    participant Barge as AudioBargeInController
    participant Player as StreamingSdlPlayer (music)

    Note over Player: music gain = 1.0
    TTS->>TTS: TtsActiveGuard ctor
    TTS->>FX: on_tts_speak_begin()
    FX->>Barge: tts_speak_begin(0.20, 80 ms)
    Barge->>Player: linear ramp 1.0 → 0.20 over 80 ms
    Note over Player: music gain ≈ 0.20 — assistant audible
    TTS-->>TTS: synthesise + play reply
    TTS->>FX: on_tts_speak_end()
    FX->>Barge: tts_speak_end(80 ms)
    Barge->>Player: linear ramp 0.20 → 1.0 over 80 ms
    Note over Player: music gain restored
```

### 5.7 Drill ready-gate (auto-advance off)

```mermaid
flowchart TD
    A[PronunciationFeedback action] -->|spoken by TTS| B{HECQUIN_DRILL_AUTO_ADVANCE}
    B -- 1 default --> C[announce next sentence immediately]
    B -- 0 --> D[set pending_drill_announce_<br>but DO NOT flush]
    D --> E((wait for user))
    E -->|"next" / "again" / "skip"| F[DrillAdvance action]
    F -->|maybe_announce_drill_| G[flush pending announce<br>+ MuteGuard mic]
    G --> H[next reference sentence spoken]
    E -->|"exit drill" / "stop drill"| I[DrillModeToggle disable<br>drop pending]
```

### 5.8 User identification + welcome-back recap

```mermaid
sequenceDiagram
    autonumber
    participant U as User
    participant L as VoiceListener
    participant Loc as LocalIntentMatcher
    participant App as LearningApp
    participant S as LearningStore

    Note over U,S: First-time identification (mid-session)
    U->>L: "I'm Mia"
    L->>Loc: match_local
    Loc-->>L: Action{IdentifyUser, param="mia"}
    L->>App: user_identified_cb_("mia")
    App->>S: upsert_user("mia")
    S-->>App: id=42
    App->>App: current_user_id_ = 42
    L-->>U: "Got it." (spoken reply)
    Note over App,S: Subsequent progress writes carry user_id=42

    Note over U,S: Next boot — welcome-back
    App->>S: last_session_pronunciation_score(limit=10, user_id=42)
    S-->>App: 78.4
    App->>S: weakest_phonemes(n=1)
    S-->>App: ["θ"]
    App-->>U: Piper: "Welcome back. Last time you scored 78.<br>Today let's work on the θ sound."
```
