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

# Never commit a populated local API config (requires `git add -f` because
# `.env/` is ignored — this catches accidental force-adds).
mapfile -t STAGED_SECRET_CFG < <(git diff --cached --name-only --diff-filter=ACM \
    | grep -E '^sound/\.env/config\.env$' || true)
if [[ ${#STAGED_SECRET_CFG[@]} -gt 0 ]]; then
  echo "pre-commit: refusing to commit sound/.env/config.env — keep secrets local;" >&2
  echo "            copy sound/config.env.example and use an untracked config.env." >&2
  exit 1
fi

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
