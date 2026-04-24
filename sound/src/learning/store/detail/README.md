# `learning/store/detail/`

Per-aggregate free-function headers. `LearningStore` forwards each of
its methods to one of these, passing the raw `sqlite3*` and the shared
`StatementCache` explicitly. Promoting the TU-local functions to a named
namespace gave us:

- a clean seam for tests (no `friend` tricks),
- one visible invariant: every SQL call runs on the single shared
  connection,
- zero public boundary change — the facade API is unchanged.

## Files

| File | Purpose |
|---|---|
| `DocumentsOps.hpp` | `upsert_document`, `is_file_already_ingested`, `record_ingested_file`, `sample_drill_sentences`. Writes documents + ingested_files + drill pool. |
| `VectorSearchOps.hpp` | `query_top_k(has_vec0, embedding, k)` — vec0 MATCH when the extension is loaded, brute-force cosine over the BLOB column when it isn't. |
| `SessionsOps.hpp` | `begin_session`, `end_session`, `record_interaction`, `touch_vocab` (uses `Vocabulary::normalise`). |
| `PronunciationOps.hpp` | `record_pronunciation_attempt`, `touch_phoneme_mastery`, `weakest_phonemes(n, min_attempts)`. |
| `ApiCallsOps.hpp` | `record_api_call`, `record_pipeline_event`. Written by `LoggingHttpClient` and the pipeline-event sinks. |

All five are guarded by `#ifdef HECQUIN_WITH_SQLITE` — when SQLite is
absent the facade short-circuits before calling the forwarder.

## Notes

- Signatures are `(sqlite3* db, learning::detail::StatementCache& cache,
  …)`. Do not add a third hidden "current user" or "current session"
  argument; those live on `LearningStore` members and get passed in
  explicitly.
- Each op is `void` / returns a small POD. No exceptions — errors are
  logged through `observability::Log` and the call becomes a no-op so
  the voice pipeline doesn't die on a disk hiccup.
