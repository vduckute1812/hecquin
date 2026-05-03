# Sound UX flow — voice-first conversation layer

**Who this is for:** Developers changing behaviour in `sound/src/voice/`, `actions/`, `ai/`, or `learning/`—anything that affects **what the user hears** beyond raw speech recognition and TTS.

**What it covers:** A four-tier “conversation quality” layer on top of the pipeline (capture → VAD → Whisper → router → Piper). It answers: *Was I heard? Is it working? What mode am I in? Can I interrupt? Should the mic be live? Is this session mine?*

**Where the plumbing lives:** See [`ARCHITECTURE.md`](./ARCHITECTURE.md).  
**Boot and end-to-end call diagrams:** See [`ux-flow/SEQUENCE_DIAGRAMS.md`](./ux-flow/SEQUENCE_DIAGRAMS.md).

---

## How to read this documentation

| If you want… | Open |
|--------------|------|
| **Big picture** (why + tiers + where files are) | This page |
| **Index of the split folder** | [`ux-flow/README.md`](./ux-flow/README.md) |
| **Each collaborator** (Earcons, wake gate, music hooks, learning schema) | [`ux-flow/COLLABORATORS.md`](./ux-flow/COLLABORATORS.md) |
| **Mermaid diagrams** (modes, utterance timelines, music/drill/user flows) | [`ux-flow/DIAGRAMS.md`](./ux-flow/DIAGRAMS.md) |
| **End-to-end sequences** (boot, one voice turn, barge-in, music streaming) | [`ux-flow/SEQUENCE_DIAGRAMS.md`](./ux-flow/SEQUENCE_DIAGRAMS.md) |
| **Environment variables** (deployer reference) | [§ Configuration](#configuration-knobs) below — also mirrored in [`README.md`](./README.md) |

---

## Contents

- [Why a UX layer?](#why-a-ux-layer)
- [Tier overview](#tier-overview)
- [Where the code lives](#where-the-code-lives)
- [Configuration knobs](#configuration-knobs)
- [Testing](#testing)
- [Related documentation](#related-documentation)
- [Glossary](#glossary)

---

## Why a UX layer?

Voice-only systems lose trust in **silence**: the user cannot see a spinner. These collaborators close the loop **without a screen**:

| User question | Mechanism | What they perceive |
|---------------|-----------|-------------------|
| Was I heard? | `Earcons` (short synthetic tones, ≤200 ms) | Soft rise when listening starts; soft fall when utterance is rejected |
| Is something happening? | `Earcons` thinking pulse + **800 ms** gate | Pulse only if routing is *actually* slow (e.g. LLM) |
| What mode am I in? | `ModeIndicator` | Short cue when switching Assistant ↔ Lesson ↔ Drill ↔ Music ↔ Asleep |
| Can I interrupt TTS? | `AbortReply` + `AudioBargeInController` | Phrases like “stop” cut Piper mid-reply |
| When does the mic count? | `WakeWordGate` | Always / wake phrase / PTT |
| Is progress tied to me? | `LearningStore` users + `IdentifyUser` | “I’m Mia” → later: “Welcome back. Last time…” |

Most behaviour is **opt-in per knob** while defaults preserve older semantics (e.g. `HECQUIN_EARCONS=0`, `HECQUIN_WAKE_MODE=ptt`, `HECQUIN_DRILL_AUTO_ADVANCE=0`, `HECQUIN_CONFIRM_CANCEL=1`).

---

## Tier overview

| Tier | Theme | Delivered (summary) |
|-----:|--------|---------------------|
| **1** | Quick wins | Earcons (start / reject / ack / offline), `AbortReply`, TTS ducks music, `Help`, Piper prewarm |
| **2** | Conversation quality | Adaptive VAD (`NoiseFloorTracker`), thinking earcon, drill advance gate + `DrillAdvance`, media controls, `ModeIndicator` on mode changes |
| **3** | Reliability | Boot capability speech (`VoiceApp::speak_capability_summary`), music cancel confirmation (`HECQUIN_CONFIRM_CANCEL`), network cooldown / fallback on repeated 5xx |
| **4** | Privacy + engagement | `WakeWordGate`, Sleep/Wake + `ListenerMode::Asleep`, per-user rows in `LearningStore`, welcome-back recap |

Details per component: [`ux-flow/COLLABORATORS.md`](./ux-flow/COLLABORATORS.md).  
Visual flows: [`ux-flow/DIAGRAMS.md`](./ux-flow/DIAGRAMS.md).

---

## Where the code lives

UX-related changes touch several trees under `sound/src/`. The duplicate `voice/` branch in older docs is merged below into **one** `voice/` section.

```
sound/src/
├── actions/
│   └── ActionKind.hpp
│       DrillAdvance, AbortReply, Help, Sleep, Wake, IdentifyUser,
│       MusicVolumeUp / Down, MusicSkip
├── ai/
│   └── LocalIntentMatcher.{hpp,cpp}
│       Patterns: drill_advance, abort, help, sleep, wake, identify_user,
│       music_volume_*, skip
├── voice/
│   ├── Earcons.{hpp,cpp}                 tone bank + thinking pulse
│   ├── WakeWordGate.{hpp,cpp}            always / wake_word / ptt
│   ├── ModeIndicator.{hpp,cpp}           default earcons; swap for GPIO/LED
│   ├── MusicSideEffects.{hpp,cpp}        duck, confirm-cancel, volume/skip
│   ├── ActionSideEffectRegistry.{hpp,cpp}  Sleep, Wake, AbortReply, ExitHome
│   ├── ListenerMode.hpp                  adds Asleep
│   ├── VoiceListener.{hpp,cpp}           wires UX collaborators + scheduling
│   └── VoiceApp.{hpp,cpp}                boot summary, Piper prewarm
└── learning/
    ├── store/
    │   ├── LearningStore.{hpp,cpp}       upsert_user, scores by user
    │   ├── LearningStoreMigrations.cpp v3 users + nullable user_id
    │   └── LearningStorePronunciation.cpp user-aware queries
    └── cli/
        └── LearningApp.{hpp,cpp}         identify callback, speak_welcome_back
```

---

## Configuration knobs

Optional environment variables; defaults keep pre–tier-1 behaviour unless noted.

| Variable | Default | Purpose |
|----------|---------|---------|
| `HECQUIN_EARCONS` | `1` | `0` disables all earcons (tones + thinking pulse). |
| `HECQUIN_EARCONS_DIR` | unset | Directory of `<name>.wav` overrides (mono int16, 22050 Hz). |
| `HECQUIN_WAKE_MODE` | `always` | `always` · `wake_word` · `ptt`. |
| `HECQUIN_WAKE_PHRASE` | `hecquin|hey hecquin|…` | Wake regex alternation. |
| `HECQUIN_WAKE_WINDOW_MS` | `8000` | After a wake hit, follow-on transcripts route for this many ms. |
| `HECQUIN_DRILL_AUTO_ADVANCE` | `1` | `0` requires explicit `DrillAdvance` (“next” / “again” / “skip”). |
| `HECQUIN_CONFIRM_CANCEL` | `0` | `1` = two-step music cancel. |
| `HECQUIN_CONFIRM_CANCEL_MS` | `1200` | Confirmation window length. |
| `HECQUIN_DUCK_GAIN` | `0.20` | Music linear gain while TTS speaks (0…1). |
| `HECQUIN_DUCK_RAMP_MS` | `80` | Ramp time in/out of duck. |
| `HECQUIN_TTS_BARGE_IN` | `0` | `1` = live-mic barge-in during TTS (see sequence docs). |

Keep this table aligned with [`README.md`](./README.md) when you add or rename variables.

---

## Testing

Coverage lives under `sound/tests/`. Important suites for UX behaviour:

| Test | Covers |
|------|--------|
| `test_local_intent_matcher.cpp` | Intent precedence (`abort` vs `music_cancel`), `drill_advance`, `identify_user` capture, sleep/wake/help |
| `test_music_side_effects.cpp` | Confirm-cancel, TTS duck idempotency, volume/skip forwarding |
| `test_utterance_router.cpp` | Router chain + Asleep + wake gate |
| `test_music_session.cpp` | Volume/skip + cancel abort |
| `test_content_fingerprint.cpp` | Isolated compile (macOS signing / no stray `libcurl` in test binary — see `cmake/sound_tests.cmake`) |

Run the full suite locally with `ctest --output-on-failure` (same targets as CI: Linux and macOS).

---

## Related documentation

| Document | Role |
|----------|------|
| [`ARCHITECTURE.md`](./ARCHITECTURE.md) | Pipeline components, layout, SQLite schema; UX augments component sections without duplicating capture→TTS flow |
| [`ux-flow/SEQUENCE_DIAGRAMS.md`](./ux-flow/SEQUENCE_DIAGRAMS.md) | Boot, voice turn, barge-in, music; links into [`ux-flow/DIAGRAMS.md`](./ux-flow/DIAGRAMS.md) for UX-specific sequences |
| [`README.md`](./README.md) | User-facing guide and configuration for operators |

---

## Glossary

| Term | Meaning |
|------|---------|
| **VAD** | Voice activity detection — when the pipeline treats audio as “speech”. |
| **TTS** | Text-to-speech (Piper). |
| **PTT** | Push-to-talk — route only while a control is held. |
| **GPIO** | Hardware pin; optional PTT or LED `ModeIndicator`. |
| **LLM** | Upstream model call when routing is slow enough to show the “thinking” earcon. |
