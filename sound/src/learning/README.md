# `learning/`

English tutor + pronunciation drill subsystem. Everything that needs an
embedding, a SQLite row, a per-phoneme score, or a pitch contour lives
under this tree. The top-level files are the coordinators and free
utilities; the heavy lifting is split across five sub-folders.

## Top-level files

| File | Purpose |
|---|---|
| `EmbeddingClient.hpp/cpp` | Gemini / OpenAI-compat embeddings client. Batched `embed_many` + single `embed`. Takes an `IHttpClient&` so tests can inject canned vectors. |
| `Ingestor.hpp/cpp` | Thin coordinator over [`ingest/`](./ingest/README.md). Public API is `run(...) → IngestReport`. |
| `TextChunker.hpp/cpp` | Standalone chunkers: `chunk_text` (prose, budget + overlap, whitespace-preferred) and `chunk_lines` (line-boundary-preserving). Reused by the `ingest/` Strategy implementations and directly by tests. |
| `RetrievalService.hpp/cpp` | Convenience wrapper around `LearningStore::query_top_k` that handles embedding + truncation. |
| `ProgressTracker.hpp/cpp` | Per-user learning log — grammar interactions + pronunciation attempts. Owns the `LearningStore` sessions lifecycle. |
| `EnglishTutorProcessor.hpp/cpp` | RAG + grammar-correction pipeline. Embeds the transcript, pulls top-K docs, calls Gemini, parses the three-line reply into a `GrammarCorrectionAction`. Uses `ai::short_reply_for_status` for consistent error replies. |
| `PronunciationDrillProcessor.hpp/cpp` | Thin coordinator for the drill. Orchestrates [`pronunciation/drill/`](./pronunciation/drill/README.md). Public API (`load`, `pick_and_announce`, `score`, `available`) is unchanged from pre-refactor. |
| `Vocabulary.hpp/cpp` | Shared `normalise(word)` — lowercase alpha + apostrophe. One source of truth for `ProgressTracker::tokenize` and `LearningStore::touch_vocab`. |

## Sub-folders

- [`cli/`](./cli/README.md) — `LearningApp` bootstrap + the three binary entry points (`english_ingest`, `english_tutor`, `pronunciation_drill`).
- [`ingest/`](./ingest/README.md) — file discovery, fingerprinting, chunking strategy, embedding batching, document persistence, CLI progress.
- [`pronunciation/`](./pronunciation/README.md) — wav2vec2 + CTC forced alignment + GOP scoring, plus the [`drill/`](./pronunciation/drill/README.md) collaborators.
- [`prosody/`](./prosody/README.md) — YIN pitch tracker + semitone-DTW intonation scorer.
- [`store/`](./store/README.md) — SQLite-backed persistence (single-connection facade) + per-aggregate detail helpers.

## Tests

- `test_embedding_client_json`, `test_text_chunker`, `test_pronunciation_drill`, `test_english_tutor_processor`, `test_drill_sentence_picker`, `test_drill_reference_audio_lru`, `test_ingest_chunking_strategy`, `test_content_fingerprint`, `test_learning_store`, plus pronunciation / prosody units.
- See [`../../ARCHITECTURE.md#testing`](../../ARCHITECTURE.md#testing) for the full matrix.

## Notes

- Every coordinator is intentionally thin (~100 LOC). Resist growing them
  back — add a new collaborator in the right sub-folder instead.
- The SQLite store is a **single-connection** facade by design. Hold
  transactions through the store helpers, not by passing raw `sqlite3*`
  around.
