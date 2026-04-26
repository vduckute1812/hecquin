# `learning/tutor/`

Single-responsibility helpers behind
[`EnglishTutorProcessor`](../EnglishTutorProcessor.hpp). The processor
itself shrunk to a ~70 LOC coordinator that just sequences these three
collaborators — same shape as `PronunciationDrillProcessor`'s drill
helpers under `pronunciation/drill/`.

## Files

| File | Purpose |
|---|---|
| `TutorContextBuilder.hpp/cpp` | Holds a reference to a `RetrievalService` plus the tutor's RAG knobs (`rag_top_k`, `rag_max_context_chars`). `build(query)` returns the bullet-list context block, capped at the configured char budget. Replaceable behind tests. |
| `TutorChatRequest.hpp/cpp` | Pure / stateless `build_chat_body(ai, user_text, context)`. Builds the OpenAI-chat-compatible JSON request body (system + user message). UTF-8 errors in ingested context are replaced with U+FFFD instead of aborting the dump. |
| `TutorReplyParser.hpp/cpp` | Pure `parse_tutor_reply(raw, fallback_original) → GrammarCorrectionAction`. Recognises `You said: …`, `Better: …`, `Reason: …` lines case-insensitively with `:` or `-` separators. When neither `Better:` nor `Reason:` matches, the entire raw reply lands in `explanation`. |

## Why split?

`EnglishTutorProcessor.cpp` used to do retrieval + JSON assembly + HTTP
+ reply parsing + progress logging in one ~150 LOC class — the same
smell that motivated the `PronunciationDrillProcessor` split. Now each
piece is a free function or a small class with one collaborator, the
processor is a thin orchestrator, and the parser has its own unit
test that needs no database, HTTP, or model.

## Tests

- `tests/learning/test_english_tutor_processor.cpp` — full end-to-end
  with a fake `IHttpClient`, real `LearningStore` in `/tmp` for RAG
  truncation assertions, and the canonical reply parse path.
- `tests/learning/tutor/test_tutor_reply_parser.cpp` — pure parser
  matrix: canonical format, case-insensitive labels, dash separator,
  partial fields, full-text fallback, whitespace trimming.
