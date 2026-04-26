# `common/`

Cross-cutting C++ utilities shared by `ai/`, `music/`, `tts/`, `voice/`,
and the executable mains. Most files are header-only; the few that need
a TU live in the `hecquin_common` static library
(`cmake/sound_common.cmake`) which is loaded *before* `piper_speech.cmake`
and `sound_libs.cmake` so every higher lib can link it.

## Files

| File | Purpose |
|---|---|
| `StringUtils.hpp` | Header-only `trim`, `to_lower_copy`, `starts_with`, `ends_with` for ASCII / UTF-8 strings. Pure, allocation-light. |
| `Utf8.hpp/cpp` | `sanitize_utf8(input) → std::string` — replaces invalid / overlong sequences with U+FFFD, skips CP-1252 bytes like 0xA0 that would otherwise crash downstream JSON parsers. |
| `EnvParse.hpp` | Header-only `parse_float / int / size / bool / read_string` for `HECQUIN_*` overrides. Single warning format (`[env] ignoring invalid <NAME>=<raw>`), no exceptions leak out. Used by `VoiceListenerConfig`, `WhisperConfig`, `YouTubeMusicConfig`. |
| `ShellEscape.hpp` | Header-only `posix_sh_single_quote(value)` — single source of truth for POSIX `sh` single-quote escaping. Replaces the duplicated `shell_escape` / `shell_quote` that used to live in `YouTubeMusicProvider.cpp` and `PiperShellBackend.cpp`. |
| `Subprocess.hpp/cpp` | RAII handle around a child process spawned via `/bin/sh -c` with stdout piped to the parent. Methods: `spawn_read`, `read_some`, `send_sigterm`, `kill_and_reap`, `detach_stdout_fd`. Centralises the `fork`/`execl`/`pipe`/`waitpid` plumbing that previously lived in `YouTubeMusicProvider`. |

## Constraints

- Pure utilities only. No third-party includes beyond libc / POSIX.
- Header-only by default; promote to `.cpp` only when there is a real
  need (e.g. `Subprocess` owns OS handles and needs a non-trivial dtor).
- Unit-test every new helper in `sound/tests/common/`. Today:
  `test_utf8.cpp`, `test_shell_escape.cpp`, `test_subprocess.cpp`.
