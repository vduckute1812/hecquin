# `learning/prosody/`

Local intonation pipeline. YIN-based pitch tracker produces an F0 + RMS
contour; the intonation scorer compares a learner contour against a
reference via banded semitone DTW plus a final-direction rule.

## Files

| File | Purpose |
|---|---|
| `PitchTracker.hpp/cpp` | YIN implementation. `track(pcm) → PitchContour { f0_hz, rms, frame_hop_ms, sample_rate }`. `f0 = 0` marks unvoiced frames. `PitchTrackerConfig` is env-free, passed at construction. |
| `IntonationScorer.hpp/cpp` | Semitone DTW between reference and candidate contours + direction rule (rising / falling / flat) on the final segment. Returns a `0..100` score with optional `reason` for mismatched contours. |

## How the drill uses them

```
reference audio (22050 Hz Piper PCM)
    └── PitchTracker(piper_rate).track()       → reference contour (cached in LRU)

user PCM (16 kHz Whisper)
    └── PitchTracker(16000).track()            → candidate contour
    └── IntonationScorer(ref, cand).score()    → intonation 0..100
```

## Tests

- `tests/test_pitch_tracker.cpp` — YIN on synthetic sine waves at several rates.
- `tests/test_intonation_scorer.cpp` — DTW + direction rule on reference / candidate pairs.

## Notes

- DTW uses row-rolling (two rows of the cost matrix) and banded Sakoe–
  Chiba constraints so memory stays `O(ref_frames)` for thousand-frame
  contours. Do not switch back to a full 2-D matrix.
- Direction thresholds are in **semitones**, not Hz, so high-pitched and
  low-pitched voices score symmetrically. Keep it that way when tuning.
