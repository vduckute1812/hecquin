# `config/ai/`

OpenAI-compatible HTTP client settings. One struct, populated once from
env vars (via `ConfigStore`) and then passed by reference into every
HTTP consumer (`ChatClient`, `EmbeddingClient`, `CommandProcessor`,
etc.).

## Files

| File | Purpose |
|---|---|
| `AiClientConfig.hpp/cpp` | POD-ish config struct populated by `from_store(ConfigStore&)` (or `from_default_config()` which reads the default env file). Holds the connection knobs only — model identifiers, derived endpoint URLs, the chosen API key, and the optional system prompts. |

### Fields on `AiClientConfig`

- `api_key` — first non-empty of `OPENAI_API_KEY`, `HECQUIN_AI_API_KEY`,
  `GEMINI_API_KEY`, `GOOGLE_API_KEY`.
- `chat_completions_url` / `embeddings_url` — derived from `<base>` +
  `/chat/completions` and `/embeddings`. `<base>` is the first non-empty
  of `OPENAI_BASE_URL`, `HECQUIN_AI_BASE_URL`, falling back to
  `https://api.openai.com/v1`.
- `model` — `HECQUIN_AI_MODEL`, then `OPENAI_MODEL`, then the built-in
  default (`gpt-4o-mini`).
- `embedding_model` / `embedding_dim` — `HECQUIN_AI_EMBEDDING_MODEL` /
  `HECQUIN_AI_EMBEDDING_DIM`, or `OPENAI_EMBEDDING_MODEL`. Defaults
  match the in-tree Gemini setup.
- `system_prompt` / `tutor_system_prompt` — populated from prompt files
  by `AppConfig`; `load_system_prompt(path)` is the helper.

The unit test `tests/config/test_ai_client_config_precedence.cpp`
pins this precedence chain.

## Used by

- `ai/ChatClient` — `api_key`, `chat_completions_url`, `model`,
  `system_prompt`.
- `ai/CommandProcessor` — owns an instance; forwards to the chat client.
- `learning/EmbeddingClient` — `embeddings_url`, `embedding_model`,
  `embedding_dim`.
- `learning/EnglishTutorProcessor` — `tutor_system_prompt` (the
  `rag_top_k` / `rag_max_context_chars` knobs live on
  `EnglishTutorConfig`, **not** here).

## What does *not* live on `AiClientConfig`

- HTTP retry / cooldown knobs. `RetryingHttpClient` reads its own env
  via `common/EnvParse` (`HECQUIN_HTTP_MAX_ATTEMPTS`,
  `HECQUIN_HTTP_INITIAL_BACKOFF_MS`, `HECQUIN_HTTP_MAX_BACKOFF_MS`,
  `HECQUIN_HTTP_BACKOFF_JITTER_PCT`). `ChatClient` reads
  `HECQUIN_CHAT_COOLDOWN_FAILURES` and
  `HECQUIN_CHAT_COOLDOWN_MS` likewise.
- RAG / tutor sizing. `rag_top_k` and `rag_max_context_chars` are on
  `learning::EnglishTutorConfig`; the curriculum paths and language
  knobs are on `LanguageLearningConfig` (see `config/AppConfig.hpp`).
- Wake / VAD knobs. Those live on `voice::VoiceListenerConfig`.

## Notes

- Keep this struct dependency-free: no curl, no nlohmann/json. Only
  plain strings + ints so tests can construct it with a `{}` literal
  or via `from_store` against an in-memory `ConfigStore`.
- Every new HTTP-connection knob lands here, not as a free env-var
  lookup at the call site. Per-feature behaviour knobs (retry,
  cooldown, RAG sizing, etc.) belong on the matching feature config.
