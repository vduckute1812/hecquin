#!/usr/bin/env bash
# Pre-commit hook for the sound module.
#
#   - Runs `clang-format -i` on every staged C/C++ file under `sound/`.
#   - Re-stages the formatted file so the commit captures the fix.
#   - Skips files that clang-format cannot process (missing .clang-format,
#     out-of-tree paths, binary artefacts, …) with a readable warning.
#
# Install via `./sound/dev.sh hooks:install` (symlinks into .git/hooks),
# or copy manually to .git/hooks/pre-commit and `chmod +x` it.

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "pre-commit: clang-format not found — skipping format check." >&2
    exit 0
fi

# Staged files under sound/, restricted to source extensions we care about.
mapfile -t FILES < <(git diff --cached --name-only --diff-filter=ACM \
    | grep -E '^sound/(src|tests)/.+\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$' \
    || true)

if [[ ${#FILES[@]} -eq 0 ]]; then
    exit 0
fi

echo "pre-commit: formatting ${#FILES[@]} staged C/C++ file(s) under sound/"
for f in "${FILES[@]}"; do
    if [[ ! -f "$f" ]]; then continue; fi
    # -style=file picks up sound/.clang-format automatically.
    clang-format -i -style=file "$f"
    git add -- "$f"
done
