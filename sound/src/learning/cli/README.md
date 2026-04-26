# `learning/cli/`

Shared bootstrap + the three executable entry points for the learning
module. `LearningApp` owns everything the `english_tutor` and
`pronunciation_drill` mains were duplicating before.

## Files

| File | Produces | Purpose |
|---|---|---|
| `LearningApp.hpp/cpp` | — | Shared bootstrap helper. Opens `LearningStore` + migrations, resolves pronunciation paths, constructs `ProgressTracker` + base `PronunciationDrillProcessor`, seeds `LocalIntentMatcherConfig`, and exposes `wire_pipeline_sink` / `wire_drill_callbacks` helpers so each `main()` only owns the mode-specific glue. |
| `StoreApiSink.hpp/cpp` | — | `make_store_api_call_sink(LearningStore&) → ApiCallSink` factory. Both `EnglishIngest` and `EnglishTutorMain` plug the resulting lambda into `LoggingHttpClient` so every embedding / chat call lands in `documents` API-call telemetry. Single source of truth for the lambda body. |
| `EnglishIngest.cpp` | `english_ingest` | Curriculum → chunks → embeddings → SQLite. Thin `main()` that constructs an `Ingestor` and prints the resulting `IngestReport`. Uses `StoreApiSink` + `cli/DefaultPaths.hpp` for telemetry + path defaults. |
| `EnglishTutorMain.cpp` | `english_tutor` | Tutor binary. Uses `LearningApp` to wire everything, installs a `TutorCallback` backed by `EnglishTutorProcessor`, starts in `ListenerMode::Lesson`. Telemetry sink built via `StoreApiSink`. |
| `PronunciationDrillMain.cpp` | `pronunciation_drill` | Drill binary. Uses `LearningApp` to wire everything, forces the listener into `ListenerMode::Drill`, relies on `wire_drill_callbacks` for the `setDrillCallback` / `setDrillAnnounceCallback` pair. |

## Why `LearningApp` exists

Both mains used to hand-roll the same 80-odd LOC of init. `LearningApp`
is the single source of truth, so adding a new shared piece of state
(e.g. the `pipeline_events` sink) touches one file, not three. See
[`../../../ARCHITECTURE.md#cli-bootstrap-learningapp`](../../../ARCHITECTURE.md#cli-bootstrap-learningapp).

## Notes

- Keep the `main()` bodies thin: construct `LearningApp`, wire
  mode-specific glue, call `listener.run()`. No business logic.
- When a new piece of wiring starts showing up in both mains, push it
  into `LearningApp` first.
