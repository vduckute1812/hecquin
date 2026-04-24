# `config/ai/`

OpenAI-compatible HTTP client settings. One struct, populated once from
env vars and then passed by reference into every HTTP consumer
(`ChatClient`, `EmbeddingClient`, `CommandProcessor`, etc.).

## Files

| File | Purpose |
|---|---|
| `AiClientConfig.hpp/cpp` | POD-ish config struct: `api_key`, `chat_completions_url`, `embeddings_url`, `model`, `embedding_model`, `embedding_dim`, `tutor_system_prompt`, `system_prompt`, retry knobs, RAG top-K. `from_default_config()` builds one from `ConfigStore` with the documented precedence (first non-empty of `OPENAI_API_KEY`, `HECQUIN_AI_API_KEY`, `GEMINI_API_KEY`, `GOOGLE_API_KEY`). |

## Used by

- `ai/ChatClient` — `api_key`, `chat_completions_url`, `model`, `system_prompt`.
- `ai/CommandProcessor` — owns an instance; forwards to the chat client.
- `learning/EmbeddingClient` — `embeddings_url`, `embedding_model`, `embedding_dim`.
- `learning/EnglishTutorProcessor` — `tutor_system_prompt`, `rag_top_k`, `rag_max_context_chars`.

## Notes

- Keep this struct dependency-free: no curl, no nlohmann/json. Only plain
  strings + ints so tests can construct it with a `{}` literal.
- Every new HTTP knob lands here, not as a free env-var lookup at the
  call site.
