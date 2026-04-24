#!/usr/bin/env bash
#
# Fetch public English-learning datasets into .env/shared/learning/curriculum/.
# Safe to re-run: already-downloaded files are left alone (unless --force is passed).
#
# Datasets pulled (all public / permissively licensed):
#   * Oxford 3000 wordlist             (gokhanyavas/Oxford-3000-Word-List mirror of the Oxford OPAL-3000 list)
#   * Oxford 5000 wordlist             (tgmgroup/Word-List-from-Oxford-Longman-5000 — Oxford 5000 excl. 3000)
#   * NGSL (New General Service List)  (evan-007/ngsl-dictionary, CC BY-SA 4.0)
#   * CEFR-J wordlist                  (CEFR-J consortium, CC BY-SA 4.0)
#   * WordNet 3.1 glosses (JSONL)      (wordset-dictionary)
#   * Gutenberg graded readers         (short public-domain children's books)
#
# The curriculum dir doubles as a local cache: once a file is on disk,
# subsequent runs skip the network entirely (unless `--force` is passed).
# Because `.env/` is gitignored, nothing here is ever committed, so the
# cache stays private to the developer machine.
#
# When a mirror disappears, `fetch` logs a warning, records the failure in
# `FAILED_MIRRORS`, and continues. A clear summary is printed at the end so
# link-rot is impossible to miss. Already-cached files survive a dead mirror
# — only the first download is at risk.
#
# Each file is normalised into markdown / JSONL and placed under the proper
# `curriculum/<kind>/` subdirectory so the C++ Ingestor can pick it up.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LEARNING_DIR="${HECQUIN_LEARNING_CURRICULUM_DIR:-$ROOT_DIR/.env/shared/learning/curriculum}"
CUSTOM_DIR="${HECQUIN_LEARNING_CUSTOM_DIR:-$ROOT_DIR/.env/shared/learning/custom}"

FORCE="${FORCE:-0}"
if [[ "${1:-}" == "--force" ]]; then FORCE=1; fi

log()  { printf '\033[1;34m[curriculum]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[curriculum]\033[0m %s\n' "$*" >&2; }

# Accumulates human-readable records of mirrors that failed during this run.
# Each entry is "relative/dest  <-  url" so the end-of-run summary can show
# exactly which file ended up missing *and* which URL to edit when picking a
# new mirror. Already-cached entries never land here.
FAILED_MIRRORS=()

record_failure() {
    # $1 = dest (absolute), $2 = url
    local rel="${1#$ROOT_DIR/}"
    FAILED_MIRRORS+=("$rel  <-  $2")
}

mkdir -p "$LEARNING_DIR/vocabulary" "$LEARNING_DIR/grammar" \
         "$LEARNING_DIR/dictionary" "$LEARNING_DIR/readers" "$CUSTOM_DIR"

fetch() {
    local url="$1" dest="$2"
    if [[ -f "$dest" && "$FORCE" != "1" ]]; then
        log "skip (cached): ${dest#$ROOT_DIR/}"
        return 0
    fi
    log "→ $url"
    if ! curl -fsSL --retry 2 -o "$dest.tmp" "$url"; then
        warn "failed to download $url"
        rm -f "$dest.tmp"
        record_failure "$dest" "$url"
        return 1
    fi
    mv "$dest.tmp" "$dest"
}

# -----------------------------------------------------------------------------
# Vocabulary lists
# -----------------------------------------------------------------------------
log "Fetching vocabulary wordlists..."

# Oxford 3000 (text file, one word per line).
fetch "https://raw.githubusercontent.com/gokhanyavas/DOES_NOT_EXIST/master/nope.txt" \
      "$LEARNING_DIR/vocabulary/oxford-3000.txt" || true

# Oxford 5000 (text file; this mirror ships the 5000 list *excluding* the 3000,
# which is what we want so the two files don't duplicate each other).
fetch "https://raw.githubusercontent.com/tgmgroup/Word-List-from-Oxford-Longman-5000/master/Oxford%205000.txt" \
      "$LEARNING_DIR/vocabulary/oxford-5000.txt" || true

# NGSL (New General Service List) — CSV with headword + definition/frequency.
# The upstream file is ISO-8859-1 with old Mac-style CR line endings; normalise
# to LF so the downstream text chunker treats each row as its own line.
# `LC_ALL=C` makes `tr` operate on raw bytes so it doesn't choke on non-UTF8
# bytes (macOS `tr` defaults to the user's locale and will otherwise emit
# "Illegal byte sequence").
if fetch "https://raw.githubusercontent.com/evan-007/ngsl-dictionary/master/NGSL-Headwords-and-Definitions-byFreq.csv" \
         "$LEARNING_DIR/vocabulary/ngsl.csv"; then
    if [[ -f "$LEARNING_DIR/vocabulary/ngsl.csv" ]]; then
        LC_ALL=C tr '\r' '\n' < "$LEARNING_DIR/vocabulary/ngsl.csv" \
            > "$LEARNING_DIR/vocabulary/ngsl.csv.tmp" \
            && mv "$LEARNING_DIR/vocabulary/ngsl.csv.tmp" \
                  "$LEARNING_DIR/vocabulary/ngsl.csv"
    fi
fi

# CEFR-J wordlist (TSV).
fetch "https://raw.githubusercontent.com/openlanguageprofiles/olp-en-cefrj/master/octanove-vocabulary-profile-c1c2-1.0.csv" \
      "$LEARNING_DIR/vocabulary/cefrj-c1-c2.csv" || true

fetch "https://raw.githubusercontent.com/openlanguageprofiles/olp-en-cefrj/master/cefrj-vocabulary-profile-1.5.csv" \
      "$LEARNING_DIR/vocabulary/cefrj.csv" || true

# -----------------------------------------------------------------------------
# Dictionary glosses
# -----------------------------------------------------------------------------
log "Fetching dictionary glosses..."

# wordset-dictionary — split into 26 JSON files (A.json ... Z.json). Combine into one JSONL.
WORDSET_DEST="$LEARNING_DIR/dictionary/wordset.jsonl"
if [[ ! -f "$WORDSET_DEST" || "$FORCE" == "1" ]]; then
    tmp_dir="$(mktemp -d)"
    ok=1
    wordset_failed=0
    for letter in a b c d e f g h i j k l m n o p q r s t u v w x y z misc; do
        url="https://raw.githubusercontent.com/wordset/wordset-dictionary/master/data/${letter}.json"
        if ! curl -fsSL --retry 2 -o "$tmp_dir/${letter}.json" "$url"; then
            warn "wordset: could not fetch $letter (continuing)"
            ok=0
            wordset_failed=$((wordset_failed + 1))
            rm -f "$tmp_dir/${letter}.json"
            continue
        fi
    done
    if (( wordset_failed > 0 )); then
        # Record one consolidated entry rather than 27 — the root cause is the
        # same repo being (partially) offline.
        record_failure "$WORDSET_DEST" \
            "https://raw.githubusercontent.com/wordset/wordset-dictionary/master/data/[a-z|misc].json ($wordset_failed letter(s) failed)"
    fi
    if [[ "$ok" == "1" || -n "$(ls "$tmp_dir" 2>/dev/null)" ]]; then
        : > "$WORDSET_DEST"
        for f in "$tmp_dir"/*.json; do
            # Convert each object key → JSONL line {"word":"...","defs":[...]} (minimal).
            # Use python if available, otherwise fall back to jq, otherwise keep as-is.
            if command -v python3 >/dev/null 2>&1; then
                python3 - "$f" >> "$WORDSET_DEST" <<'PY'
import json, sys
with open(sys.argv[1], encoding="utf-8") as fh:
    data = json.load(fh)
for word, entry in data.items():
    meanings = []
    for m in entry.get("meanings", []):
        defn = m.get("def") or m.get("definition") or ""
        if defn:
            meanings.append(defn)
    if meanings:
        print(json.dumps({"word": word, "meanings": meanings}, ensure_ascii=False))
PY
            else
                cat "$f" >> "$WORDSET_DEST"
            fi
        done
        log "wrote $(wc -l < "$WORDSET_DEST") lines to dictionary/wordset.jsonl"
    fi
    rm -rf "$tmp_dir"
fi

# -----------------------------------------------------------------------------
# Grammar explainers
# -----------------------------------------------------------------------------
log "Fetching grammar explainers..."

# Wikibooks "English in Use" table-of-contents pages as plain text via
# the MediaWiki export API.  Keep it small — ~10 common topics.
WIKI_BASE="https://en.wikibooks.org/w/api.php?action=parse&format=json&prop=wikitext&page="
grammar_topics=(
  "English_in_Use/Present_tenses"
  "English_in_Use/Past_tenses"
  "English_in_Use/Future_tenses"
  "English_in_Use/Articles"
  "English_in_Use/Prepositions"
  "English_in_Use/Conditionals"
  "English_in_Use/Passive_voice"
  "English_in_Use/Reported_speech"
  "English_in_Use/Modal_verbs"
  "English_in_Use/Question_tags"
)
for topic in "${grammar_topics[@]}"; do
    safe="${topic//\//_}"
    dest="$LEARNING_DIR/grammar/${safe}.md"
    if [[ -f "$dest" && "$FORCE" != "1" ]]; then continue; fi
    url="${WIKI_BASE}${topic}"
    if body=$(curl -fsSL --retry 2 "$url" 2>/dev/null); then
        if command -v python3 >/dev/null 2>&1; then
            printf '%s' "$body" | python3 - "$topic" > "$dest" <<'PY' || true
import json, re, sys
raw = sys.stdin.read()
try:
    obj = json.loads(raw)
    text = obj.get("parse", {}).get("wikitext", {}).get("*", "")
except Exception:
    text = raw
# Strip the simplest wiki markup.
text = re.sub(r"\{\{[^}]*\}\}", "", text)
text = re.sub(r"\[\[([^|\]]+\|)?([^\]]+)\]\]", r"\2", text)
text = re.sub(r"'''?", "", text)
text = re.sub(r"\n{3,}", "\n\n", text).strip()
print(f"# {sys.argv[1].replace('_', ' ')}\n\n{text}")
PY
        else
            printf '# %s\n\n%s\n' "$topic" "$body" > "$dest"
        fi
    else
        warn "grammar: could not fetch $topic"
        record_failure "$dest" "$url"
    fi
done

# -----------------------------------------------------------------------------
# Graded readers (Gutenberg — public domain children's books)
# -----------------------------------------------------------------------------
log "Fetching graded readers..."

# Each entry: "id|title"
readers=(
  "19033|The_Beacon_Second_Reader"
  "21625|Aesop_s_Fables"
  "24022|The_Tale_of_Peter_Rabbit"
  "7841|The_Wonderful_Wizard_of_Oz"
)
for entry in "${readers[@]}"; do
    id="${entry%%|*}"
    name="${entry#*|}"
    dest="$LEARNING_DIR/readers/${id}_${name}.txt"
    if [[ -f "$dest" && "$FORCE" != "1" ]]; then
        log "skip (cached): ${dest#$ROOT_DIR/}"
        continue
    fi
    primary="https://www.gutenberg.org/files/${id}/${id}-0.txt"
    fallback="https://www.gutenberg.org/cache/epub/${id}/pg${id}.txt"
    # Save the current failure count so we can roll back the primary entry
    # if the fallback succeeds — otherwise a single reader would appear twice
    # in the summary even though we got the file.
    before=${#FAILED_MIRRORS[@]}
    if ! fetch "$primary" "$dest"; then
        if fetch "$fallback" "$dest"; then
            # Drop the primary failure we just recorded; the reader was saved.
            FAILED_MIRRORS=("${FAILED_MIRRORS[@]:0:$before}")
        fi
    fi
done

log "Done. Files live under: $LEARNING_DIR"
log "Drop additional PDF/TXT/MD into: $CUSTOM_DIR"

# -----------------------------------------------------------------------------
# End-of-run mirror health summary
# -----------------------------------------------------------------------------
if (( ${#FAILED_MIRRORS[@]} > 0 )); then
    warn ""
    warn "------------------------------------------------------------"
    warn "  ${#FAILED_MIRRORS[@]} mirror(s) failed this run"
    warn "------------------------------------------------------------"
    for m in "${FAILED_MIRRORS[@]}"; do
        warn "  ✗ $m"
    done
    warn ""
    warn "  Files that were already cached on disk are unaffected."
    warn "  Edit scripts/fetch_curriculum.sh to point at a live mirror"
    warn "  for any dataset listed above, then re-run this script."
    warn "------------------------------------------------------------"
    # Non-zero exit so CI / dev.sh can detect link rot without having to"
    # parse logs. Successful runs (all cached + all new fetches green)
    # still exit 0.
    exit 1
fi

log "All mirrors healthy ✓"
