# UX collaborators (implementation detail)

Companion to the overview in [`UX_FLOW.md`](../UX_FLOW.md).  
This file describes **what each UX component does**, where it sits in the pipeline, and how it connects to storage and actions.

**Terms:** VAD = voice activity detection · TTS = text-to-speech · PTT = push-to-talk · GPIO = general-purpose I/O · LLM = large language model call upstream of routing.

---

## 4. UX collaborators in detail

### 4.1 `Earcons` — non-spoken audio acknowledgements

Cue bank (`enum class Earcons::Cue`):

| Cue              | Sound              | When it plays |
|------------------|--------------------|---------------|
| `StartListening` | Rising blip        | VAD opened for this utterance |
| `VadRejected`    | Soft falling blip  | Secondary gate dropped the utterance |
| `Thinking`       | Soft pulse (~0.7 Hz) | LLM / slow route still in flight |
| `NetworkOffline` | Warble             | Boot or repeated 5xx / timeout |
| `Acknowledge`    | Short chirp        | `AbortReply` or mode toggles before TTS |
| `Sleep`          | Falling chord      | Entering `Asleep` |
| `Wake`           | Rising chord       | Leaving `Asleep` |

Tones are synthesised in-process, so no `.wav` assets ship by default. Optional override directory `HECQUIN_EARCONS_DIR` can supply custom WAVs (mono int16, 22050 Hz) keyed by name.

The thinking pulse is armed by a **+800 ms** one-shot scheduler around `UtteranceRouter::route(...)`. Fast paths (local intent, cached answer) return before the deadline, so the user does not hear “still working”; only slow LLM round-trips trigger it.

### 4.2 `WakeWordGate` — Always / WakeWord / Ptt

| Mode | Env | Behaviour |
|------|-----|-----------|
| Always | `HECQUIN_WAKE_MODE=always` | Every transcript routes (default; legacy behaviour). |
| Wake word | `HECQUIN_WAKE_MODE=wake_word` | Transcript must start with—or arrive within `HECQUIN_WAKE_WINDOW_MS` (default 8000) after—a wake phrase (`HECQUIN_WAKE_PHRASE`). The phrase is stripped before the router sees the text. |
| Push-to-talk | `HECQUIN_WAKE_MODE=ptt` | Routes only while a GPIO pin (or `set_ptt_pressed`) is held. |

The gate runs **after** Whisper and **before** the router and side-effect registry, so it stays aligned with the listener’s mode state machine.

### 4.3 `ModeIndicator` — short cue per mode change

```cpp
mode_indicator_->notify(ListenerMode::Drill);   // plays Drill-tinted earcon
```

The default implementation is a small mode-tinted variant of `Earcons::Cue::StartListening`. GPIO or LED implementations subclass `notify`; `VoiceListener::set_mode_indicator(...)` swaps the implementation without changing call sites.

| Mode | Default cue idea |
|------|------------------|
| `Assistant` | Baseline “listening” tint |
| `Lesson` / `Drill` / `Music` | Distinct tint per mode (same mechanical cue, different tone/shape) |
| `Asleep` | Uses `Sleep` / `Wake` earcons on transitions, not the generic mode blip |

### 4.4 `MusicSideEffects` — extended hooks

Existing hooks `on_playback_started`, `on_pause`, `on_resume`, `on_cancel` were extended with:

| Hook | Tier | Role |
|------|------|------|
| `on_volume_up` / `on_volume_down` | 2 | Forward `VolumeStepCallback(±1)`; default `YouTubeMusicProvider` applies via SDL gain. |
| `on_skip` | 2 | Forward `SkipCallback()`; provider may treat as stop + next pick. |
| `on_tts_speak_begin` / `on_tts_speak_end` | 1 | Duck music to `tts_duck_gain_` while Piper speaks; restore when finished. |
| `set_confirm_cancel` | 3 | Two-step cancel: first `MusicCancel` ducks and arms a window; second within window aborts. |

`apply_env_overrides()` reads `HECQUIN_CONFIRM_CANCEL` and `HECQUIN_CONFIRM_CANCEL_MS` so deployments can enable confirmation without recompiling.

### 4.5 `ActionSideEffectRegistry` — additional rows

| Action | Mode change / side effect | Notes |
|--------|---------------------------|--------|
| `LessonModeToggle` | `EnterIfEnable` Lesson | |
| `DrillModeToggle` | `EnterIfEnable` Drill | Sets pending drill state |
| `MusicSearchPrompt` | `Enter` Music | |
| `MusicPlayback` | `ExitTo` Music | `on_playback_started` |
| `MusicNotFound` | `ExitTo` Music | `on_playback_not_found` |
| `MusicCancel` | `ExitTo` Music | `on_cancel` |
| `MusicPause` / `MusicResume` | None | `on_pause` / `on_resume` |
| `MusicVolumeUp` / `MusicVolumeDown` | None | `on_volume_up` / `down` |
| `MusicSkip` | None | `on_skip` |
| `AbortReply` | None | `aborts_tts=true` |
| `Sleep` | `Enter` Asleep | |
| `Wake` | `ExitHome` Asleep | |

`aborts_tts` causes the listener to call `barge_.abort_tts_now()`. `ExitHome` returns to the user’s home mode when leaving Asleep; if `home_mode_ == Asleep`, it falls through to Assistant so the session cannot get stuck.

### 4.6 LearningStore — per-user namespacing (schema v3)

```sql
-- v3 (Tier-4)
CREATE TABLE users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  display_name TEXT NOT NULL UNIQUE,
  voice_embedding_blob BLOB,                     -- reserved: speaker-id v2
  created_at INTEGER NOT NULL
);

-- NULL user_id = legacy “default user”
ALTER TABLE interactions           ADD COLUMN user_id INTEGER REFERENCES users(id);
ALTER TABLE pronunciation_attempts ADD COLUMN user_id INTEGER REFERENCES users(id);
```

Migrations are idempotent: `pragma_table_info(...)` before each `ADD COLUMN`, so re-running on an already migrated database is safe.

| Method | Returns | Used by |
|--------|---------|---------|
| `upsert_user(display_name)` | optional id | `IdentifyUser` → `LearningApp` |
| `last_session_pronunciation_score(limit, user_id?)` | optional average | `speak_welcome_back()` |
