#!/usr/bin/env bash
#
# Fetch public English-learning datasets into .env/shared/learning/curriculum/.
# Safe to re-run: already-downloaded files are left alone (unless --force is passed).
#
# Datasets pulled (all public / permissively licensed):
#   * Oxford 3000 / 5000 wordlists     (mrcoder007/English-Vocab-3000-5000 fork of the Oxford lists)
#   * NGSL (New General Service List)  (browsefire/ngsl-list, CC BY-SA 4.0)
#   * CEFR-J wordlist                  (CEFR-J consortium, CC BY-SA 4.0)
#   * WordNet 3.1 glosses (JSONL)      (wordset-dictionary)
#   * Gutenberg graded readers         (short public-domain children's books)
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

mkdir -p "$LEARNING_DIR/vocabulary" "$LEARNING_DIR/grammar" \
         "$LEARNING_DIR/dictionary" "$LEARNING_DIR/readers" "$CUSTOM_DIR"

fetch() {
    local url="$1" dest="$2"
    if [[ -f "$dest" && "$FORCE" != "1" ]]; then
        log "skip (exists): ${dest#$ROOT_DIR/}"
        return 0
    fi
    log "→ $url"
    if ! curl -fsSL --retry 2 -o "$dest.tmp" "$url"; then
        warn "failed to download $url"
        rm -f "$dest.tmp"
        return 1
    fi
    mv "$dest.tmp" "$dest"
}

# -----------------------------------------------------------------------------
# Vocabulary lists
# -----------------------------------------------------------------------------
log "Fetching vocabulary wordlists..."

# Oxford 3000 / 5000 (text file, one word per line)
fetch "https://raw.githubusercontent.com/jnishiyama/oxford-3000/master/oxford-3000.txt" \
      "$LEARNING_DIR/vocabulary/oxford-3000.txt" || true

fetch "https://raw.githubusercontent.com/jnishiyama/oxford-5000/master/oxford-5000.txt" \
      "$LEARNING_DIR/vocabulary/oxford-5000.txt" || true

# NGSL (New General Service List) — CSV with headword + frequency.
fetch "https://raw.githubusercontent.com/browsefire/ngsl-list/master/ngsl-list.csv" \
      "$LEARNING_DIR/vocabulary/ngsl.csv" || true

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
    for letter in a b c d e f g h i j k l m n o p q r s t u v w x y z misc; do
        url="https://raw.githubusercontent.com/wordset/wordset-dictionary/master/data/${letter}.json"
        if ! curl -fsSL --retry 2 -o "$tmp_dir/${letter}.json" "$url"; then
            warn "wordset: could not fetch $letter (continuing)"
            ok=0
            continue
        fi
    done
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
    if [[ -f "$dest" && "$FORCE" != "1" ]]; then continue; fi
    url="https://www.gutenberg.org/files/${id}/${id}-0.txt"
    if ! fetch "$url" "$dest"; then
        # Try alternate mirror path.
        fetch "https://www.gutenberg.org/cache/epub/${id}/pg${id}.txt" "$dest" || true
    fi
done

log "Done. Files live under: $LEARNING_DIR"
log "Drop additional PDF/TXT/MD into: $CUSTOM_DIR"
