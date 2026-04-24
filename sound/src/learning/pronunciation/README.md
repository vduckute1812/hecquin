# `learning/pronunciation/`

Per-phoneme scoring: grapheme-to-phoneme, acoustic model (wav2vec2),
forced alignment (CTC Viterbi), and Goodness-of-Pronunciation scoring.
The [`drill/`](./drill/README.md) sub-folder owns the collaborators that
the drill processor composes on top of these primitives.

## Files

| File | Purpose |
|---|---|
| `PhonemeTypes.hpp` | Shared data structs: `Emissions`, `AlignSegment`, `ScoredPhoneme`, `ScoredWord`, `DrillScore`. |
| `PhonemeVocab.hpp/cpp` | IPA ↔ id tokenizer built from HuggingFace-style `vocab.json`. Greedy longest-match on the IPA string. |
| `PhonemeModel.hpp/cpp` | `onnxruntime` wrapper around the wav2vec2 phoneme-CTC ONNX model. Optional: guarded by `HECQUIN_WITH_ONNX`; `load()` returns false when the runtime is unavailable. |
| `G2P.hpp/cpp` | espeak-ng `--ipa=3` → target phoneme ids. Spawns espeak-ng as a subprocess; voice is driven by `HECQUIN_ESPEAK_VOICE`. |
| `CtcAligner.hpp/cpp` | Viterbi forced alignment on the emission trellis. Uses row-rolling to keep the score matrix flat; still returns a full backpointer matrix for the traceback. |
| `PronunciationScorer.hpp/cpp` | logp → 0..100 per phoneme / word / overall. Supports optional per-IPA calibration anchors (`calibration.json`) and global `{min_logp, max_logp}` defaults. |

## Sub-folders

- [`drill/`](./drill/README.md) — sentence picker, reference audio + LRU, scoring pipeline (Template Method), progress logger.

## Data flow inside the drill

```
transcript + PCM
    │
    ├── G2P → IPA plan
    ├── PhonemeModel.infer(PCM) → Emissions
    ├── CtcAligner.align(plan, emissions) → AlignSegments
    ├── PronunciationScorer.score(segments, emissions) → DrillScore (0..100)
    └── ScoredPhoneme[] → phoneme_mastery updates (via LearningStore)
```

## Tests

- `tests/test_phoneme_vocab.cpp`, `test_ctc_aligner.cpp`, `test_pronunciation_scorer.cpp`.

## Notes

- Every unit here is **pure** modulo the ONNX model load — no SDL, no
  Piper, no HTTP. Tests inject `Emissions` directly so the acoustic model
  is never needed.
- When the ONNX runtime is missing, `PhonemeModel::load()` returns false
  and the drill reports `"pronunciation scoring unavailable — install
  onnxruntime"`. Handle the false at the call site, don't assert.
