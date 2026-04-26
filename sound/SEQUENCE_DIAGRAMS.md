# `sound/` Sequence Diagrams

End-to-end Mermaid sequence diagrams for the `sound/` module.  This file
**only documents flows that are not already drawn elsewhere** — every
existing per-module diagram is linked from the index below instead of
re-rendered.

## Existing diagrams (single source of truth)

| Flow                                   | Location                                                                                          |
|----------------------------------------|---------------------------------------------------------------------------------------------------|
| `VoiceListener::run` main loop         | [`src/voice/README.md`](./src/voice/README.md#sequence-diagram--voicelistenerrun-loop)            |
| `piper_speak_and_play_streaming` (legacy) | [`src/tts/README.md`](./src/tts/README.md#sequence-diagram--piper_speak_and_play_streaming)    |
| `CommandProcessor::process` (chat fallback) | [`src/ai/README.md`](./src/ai/README.md#sequence-diagram--commandprocessorprocess)            |
| `EnglishTutorProcessor::process` (RAG) | [`src/learning/README.md`](./src/learning/README.md#sequence-diagram--englishtutorprocessorprocess) |
| `PronunciationDrillProcessor::score`   | [`src/learning/pronunciation/drill/README.md`](./src/learning/pronunciation/drill/README.md#sequence-diagram--pronunciationdrillprocessorscore) |
| `ListenerMode` state transitions       | [`src/voice/README.md`](./src/voice/README.md#state-diagram--listenermode)                        |
| `UtteranceCollector` collect FSM       | [`src/voice/README.md`](./src/voice/README.md#state-diagram--utterancecollector)                  |
| `StreamingSdlPlayer` playback FSM      | [`src/tts/README.md`](./src/tts/README.md#state-diagram--streamingsdlplayer)                      |

The diagrams below cover the **gaps**: boot, cross-cutting voice-turn
choreography, the live-mic TTS barge-in path, and music streaming with
mid-song voice control.

---

## 1. Boot — bringing one voice executable up

Shared startup path for `voice_detector`, `english_tutor`, and
`pronunciation_drill`.  `english_ingest` and `text_to_speech` skip the
listener wiring; everything else funnels through `VoiceApp`.

```mermaid
sequenceDiagram
    autonumber
    participant Main as main()
    participant App as VoiceApp
    participant Cfg as AppConfig (ConfigStore)
    participant W as WhisperEngine
    participant Cap as AudioCapture (SDL2)
    participant Lst as VoiceListener
    participant Music as MusicWiring
    participant Tut as EnglishTutorProcessor
    participant Drl as PronunciationDrillProcessor

    Main->>Cfg: load() + apply_env_overrides()
    Main->>App: VoiceApp(cfg, options)
    App->>W: open(model_path, WhisperConfig)
    App->>Cap: open(AudioCaptureConfig 16 kHz mono)
    App->>Lst: VoiceListener(cap, whisper, cfg, ...)

    opt cfg.music.provider != none
        Main->>Music: install_music_wiring(listener, cfg.music)
        Music->>Music: MusicFactory::make_provider_from_config()
        Music->>Lst: setMusicCallback + abort/pause/resume hooks (via MusicSideEffects)
    end

    opt english_tutor binary
        Main->>Tut: build TutorContextBuilder + ChatHttpClient chain
        Main->>Lst: setTutorCallback(tutor.process_async)
    end
    opt pronunciation_drill binary
        Main->>Drl: load() — resolve_sentence_pool_ + prime_picker_
        Main->>Lst: setDrillCallback(drill.score) + drill_announce_cb
    end

    Main->>Lst: run()
    Note over Lst: enters main loop — see voice/README.md
```

---

## 2. Cross-cutting voice turn — who calls whom across modules

The `voice/README.md` loop diagram shows the per-iteration shape.  This
diagram zooms out and shows **how the four mode handlers fan out into
the rest of the codebase** for one accepted utterance.  Per-handler
detail is in the linked diagrams (don't duplicate them here).

```mermaid
sequenceDiagram
    autonumber
    participant Lst as VoiceListener
    participant R as UtteranceRouter
    participant Loc as LocalIntentMatcher
    participant Mus as MusicSession
    participant Drl as PronunciationDrillProcessor
    participant Tut as EnglishTutorProcessor
    participant Cmd as CommandProcessor (LLM)
    participant TTS as TtsResponsePlayer
    participant FX as MusicSideEffects + ActionSideEffectRegistry

    Note over Lst: utterance already passed VAD + Whisper<br>(see voice/README.md)
    Lst->>R: route({transcript, pcm})
    R->>Loc: match_local(transcript)

    alt local intent (mode toggle / device / story / music ctl)
        Loc-->>R: Action
    else mode = Music
        R->>Mus: handle(transcript)
        Note right of Mus: see Diagram 4
        Mus-->>R: MusicPlayback / MusicNotFound
    else mode = Drill
        R->>Drl: score(pcm, transcript)
        Note right of Drl: see drill/README.md
        Drl-->>R: PronunciationFeedback Action
    else mode = Lesson
        R->>Tut: process(transcript)
        Note right of Tut: see learning/README.md
        Tut-->>R: GrammarCorrection Action
    else fallback
        R->>Cmd: process(transcript)
        Note right of Cmd: see ai/README.md
        Cmd-->>R: ChatReply / ExternalApi Action
    end
    R-->>Lst: Result{action, from_local_intent}

    Lst->>FX: apply_local_intent_side_effects_(action)
    Note over FX: ActionSideEffectRegistry mode flip<br>+ MusicSideEffects collector/barge lockstep
    Lst->>TTS: speak(action, mode_label)
    Note right of TTS: see Diagram 3 for barge-in,<br>tts/README.md for legacy
    opt drill mode and PronunciationFeedback
        Lst->>Drl: drill_announce_cb (next target sentence)
    end
```

---

## 3. TTS playback — `speak()` with live-mic barge-in

`tts/README.md` already shows the basic `piper_speak_and_play_streaming`
sequence (Piper child + push callback + prebuffer).  This diagram
adds the **barge-in delta**: the abort fuse, raised VAD threshold, and
how the read loop bails when the user starts speaking over the
assistant.  The `else` branch falls back to the legacy mute-mic path
already drawn in tts/README.md.

```mermaid
sequenceDiagram
    autonumber
    participant TTS as TtsResponsePlayer
    participant Col as UtteranceCollector
    participant Barge as AudioBargeInController
    participant Pipe as PlayPipeline
    participant Spawn as run_pipe_synth
    participant Player as StreamingSdlPlayer

    TTS->>TTS: sanitise + label-print
    alt cfg.barge_in_live_mic = false
        Note over TTS: legacy path — see tts/README.md
        TTS->>Col: MuteGuard (pause + clearBuffer)
        TTS->>Pipe: speak_with_muted_mic_(text)
    else live-mic barge-in
        TTS->>Col: TtsActiveGuard (raise VAD threshold, mark tts_active)
        TTS->>Barge: set_tts_aborter(&abort_tts)
        TTS->>Col: clearBuffer()
        TTS->>Pipe: speak_with_barge_in_(text, &abort_tts)

        Pipe->>Player: start(22050)
        Pipe->>Spawn: run_pipe_synth(text, model, on_samples)
        loop pump_stdout
            Spawn->>Player: push(pcm, n)
            opt user starts speaking
                Barge->>Barge: voice detected over speaker
                Barge->>Barge: invoke aborter
                Barge-->>TTS: abort_tts.store(true)
                Player-->>Spawn: on_samples returns false
                Note over Spawn: keeps draining so piper<br>can flush + exit cleanly
            end
        end
        Spawn->>Spawn: reap_child (waitpid through EINTR)
        Spawn-->>Pipe: PiperSpawnResult
        Pipe->>Pipe: log_piper_wait_status(status)
        Pipe->>Player: finalize_streaming_player(player, aborted)
        alt aborted
            Player->>Player: stop() (drop tail)
        else normal
            Player->>Player: finish() + wait_until_drained() + stop()
        end

        TTS->>Barge: set_tts_aborter(nullptr)
        TTS->>Col: TtsActiveGuard dtor (restore VAD threshold + clearBuffer)
    end
```

---

## 4. Music — streaming + mid-song voice commands

`MusicSession::handle` is non-blocking: search runs synchronously, then
`provider.play()` is dispatched to a background thread so the listener
keeps capturing voice and routing `MusicCancel / MusicPause /
MusicResume` against the running song.  The mic stays **live** during
playback (no `MuteGuard`) — speaker bleed is suppressed by the
`MusicSideEffects` lock-step on `UtteranceCollector` +
`AudioBargeInController`.

```mermaid
sequenceDiagram
    autonumber
    participant Lst as VoiceListener
    participant R as UtteranceRouter
    participant MS as MusicSession
    participant Prov as YouTubeMusicProvider
    participant YT as yt-dlp + ffmpeg
    participant Player as StreamingSdlPlayer
    participant FX as MusicSideEffects
    participant Col as UtteranceCollector
    participant Barge as AudioBargeInController
    participant Loc as LocalIntentMatcher

    Note over Lst: ListenerMode flipped to Music<br>by local intent "open music"
    Lst->>R: route({"some song name"})
    R->>MS: handle(query)
    MS->>Prov: search(query) — yt::build_search_command
    Prov->>YT: ytsearch1: shell command
    YT-->>Prov: stdout (title \t url ...)
    Prov-->>MS: optional<MusicTrack>

    alt no match
        MS-->>R: MusicNotFound Action
    else match
        MS->>MS: start_playback_thread_(track)
        MS->>MS: abort prior song (provider.stop + join)
        MS->>+Prov: play(track) [worker thread]
        Note over MS: handle() returns immediately
        MS-->>R: MusicPlayback Action ("Now playing ...")
        Lst->>FX: on_playback_started()
        FX->>Col: set_external_audio_active(true)
        FX->>Barge: set_music_active(true)

        par worker thread streams audio
            Prov->>YT: build_playback_command + spawn
            YT->>Player: int16 PCM (44.1 kHz)
        and listener keeps capturing voice
            loop while song streams
                Lst->>R: route(transcript)
                R->>Loc: match_local
                alt "stop / cancel music"
                    Loc-->>R: MusicCancel
                    Lst->>FX: on_cancel()
                    FX->>MS: abort()
                    FX->>Col: set_external_audio_active(false) + reset_noise_floor()
                    FX->>Barge: set_music_active(false)
                    MS->>Prov: stop()
                    Prov->>YT: SIGTERM child pid
                    Prov-->>-MS: play() returns
                    MS->>MS: join worker
                else "pause music"
                    Lst->>FX: on_pause()
                    FX->>Prov: pause() (SDL_PauseAudioDevice)
                    FX->>Col: set_external_audio_active(false)
                else "resume music"
                    Lst->>FX: on_resume()
                    FX->>Prov: resume()
                    FX->>Col: set_external_audio_active(true)
                else user speaks over song (barge-in duck)
                    Barge->>Prov: set_gain_target(duck_gain, ramp_ms)
                    Note over Player: linear gain ramp on next callback
                end
            end
        end
    end
```

---

## How the diagrams connect

- **(1) Boot** ends at `Lst.run()` — enter the loop in
  [`voice/README.md`](./src/voice/README.md#sequence-diagram--voicelistenerrun-loop).
- The loop's accepted-utterance branch enters **(2)** here.
- **(2)** dispatches into one of the per-handler diagrams listed in the
  index, then back into the side-effect lane and **(3)**.
- **(2)** entering Music mode expands into **(4)**.
- `MusicSideEffects.on_*` calls in **(4)** are the same hooks invoked
  from `apply_local_intent_side_effects_` in **(2)**.
