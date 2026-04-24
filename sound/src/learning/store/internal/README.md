# `learning/store/internal/`

Private RAII + helper glue for the SQLite wrappers. Headers here are
visible only to other files under `learning/store/` — they never make it
into the public include graph.

## Files

| File | Purpose |
|---|---|
| `SqliteHelpers.hpp` | `StmtGuard` (finalise-on-scope), `Transaction` (BEGIN / COMMIT / ROLLBACK), `prepare_or_log(db, sql)` factory, `bind_text` / `bind_blob` / `bind_int64` / `bind_double` helpers, and `StatementCache` (with `CachedStmt` RAII wrapper that resets / rebinds without finalising the compiled plan). |

## Notes

- These are header-only so every `LearningStore*.cpp` sees the same
  inline definitions; no `learning_store_internal` library needed.
- Do not expose any of these types through `LearningStore.hpp`. Callers
  outside `store/` must not depend on `StmtGuard` or `Transaction` —
  they should go through the facade.
- `StatementCache` is keyed by the SQL text literal. Use the same string
  spelling at every call site for a given query or you will split the
  cache entry.
