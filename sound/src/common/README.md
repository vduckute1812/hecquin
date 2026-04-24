# `common/`

Dependency-free header-only helpers. If a snippet needs to be shared by
two unrelated packages and doesn't justify a `.cpp`, it goes here.

## Files

| File | Purpose |
|---|---|
| `StringUtils.hpp` | `trim`, `to_lower_copy`, `starts_with`, `ends_with` for ASCII / UTF-8 strings. Pure, allocation-light. |
| `Utf8.hpp` | `sanitize_utf8(input) → std::string` — replaces invalid / overlong sequences with U+FFFD, skips CP-1252 bytes like 0xA0 that would otherwise crash downstream JSON parsers. |

## Constraints

- Header-only. No `.cpp` files in this folder.
- No third-party includes. Keep the fan-in edge cheap.
- Unit-test every new helper in `sound/tests/`. Today: `test_utf8.cpp`.
