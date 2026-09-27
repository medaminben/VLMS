#!/usr/bin/env bash
# Pack gitignored runtime data for CI / release installer builds.
#
# Creates dist/VLMS_installer_data.zip containing:
#   database/vlms.db
#   resources/books/
#   resources/members/
#
# Upload the zip as a release asset; CI fetch_installer_data.ps1 downloads it.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DB="$ROOT/database/vlms.db"
BOOKS="$ROOT/resources/books"
MEMBERS="$ROOT/resources/members"
OUT_DIR="$ROOT/dist"
ZIP="$OUT_DIR/VLMS_installer_data.zip"
STAGE="$(mktemp -d)"

cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

if [[ ! -f "$DB" ]]; then
  echo "Error: missing $DB — run scripts/import_catalog.py first." >&2
  exit 1
fi

if [[ ! -d "$BOOKS" ]] || [[ -z "$(ls -A "$BOOKS" 2>/dev/null || true)" ]]; then
  echo "Error: missing or empty $BOOKS" >&2
  exit 1
fi

# Refuse to pack a catalogue at the wrong schema. This zip is what the Windows
# installer build bundles, and shipping a database the application has to
# migrate on first launch is the failure this whole check exists to catch --
# the 0.1.0 zip held a database written before PRAGMA user_version existed.
python3 - "$DB" "$ROOT/database/schema.sql" <<'PY'
import re, sqlite3, sys

db_path, schema_path = sys.argv[1], sys.argv[2]

with open(schema_path, encoding="utf-8") as handle:
    stamps = re.findall(r"(?im)^\s*PRAGMA\s+user_version\s*=\s*(\d+)", handle.read())
if not stamps:
    sys.exit(f"Error: no 'PRAGMA user_version' in {schema_path}")
expected = int(stamps[-1])

connection = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
actual = connection.execute("PRAGMA user_version").fetchone()[0]
connection.close()

if actual != expected:
    sys.exit(
        f"Error: {db_path} is at schema {actual}, but schema.sql ships {expected}.\n"
        "Open the database once with this build to migrate it, then re-run this script."
    )
print(f"Database schema {actual} matches schema.sql.")
PY

mkdir -p "$STAGE/database" "$STAGE/resources/books" "$STAGE/resources/members"
cp "$DB" "$STAGE/database/vlms.db"
cp -a "$BOOKS/." "$STAGE/resources/books/"
if [[ -d "$MEMBERS" ]]; then
  cp -a "$MEMBERS/." "$STAGE/resources/members/"
fi

mkdir -p "$OUT_DIR"
rm -f "$ZIP"
(
  cd "$STAGE"
  zip -r "$ZIP" database resources
)

size="$(du -h "$ZIP" | cut -f1)"
echo "Created $ZIP ($size)"
echo "Upload as a GitHub release asset for CI installer builds."
