# UX documentation split

This folder holds **long-form** material for the voice-first UX layer. Start with the summary in
[`../UX_FLOW.md`](../UX_FLOW.md), then use:

| File | Contents |
|------|----------|
| [`COLLABORATORS.md`](./COLLABORATORS.md) | `Earcons`, `WakeWordGate`, `ModeIndicator`, `MusicSideEffects`, action registry, `LearningStore` (v3) |
| [`DIAGRAMS.md`](./DIAGRAMS.md) | Mermaid mode machine, per-utterance timelines, sleep/wake, music confirm-cancel, drill gate, user ID flow |
| [`SEQUENCE_DIAGRAMS.md`](./SEQUENCE_DIAGRAMS.md) | Boot, cross-cutting voice turn, TTS barge-in, music streaming + mid-song commands (cross-module sequences) |

Component internals and SQLite: [`../ARCHITECTURE.md`](../ARCHITECTURE.md).
