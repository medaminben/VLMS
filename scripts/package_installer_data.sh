#!/usr/bin/env bash
# Pack the sample cover images and member photographs, which are not tracked.
#
# Creates dist/VLMS_installer_data.zip containing:
#   resources/books/
#   resources/members/
#
# database/vlms.db is tracked and is not part of the zip, so a download can
# never replace a newer database in the checkout.
#
# Upload the zip as an asset on the release named by DataTag in
# scripts/fetch_installer_data.ps1 (and DATA_TAG in fetch_runtime_data.sh).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BOOKS="$ROOT/resources/books"
MEMBERS="$ROOT/resources/members"
OUT_DIR="$ROOT/dist"
ZIP="$OUT_DIR/VLMS_installer_data.zip"
STAGE="$(mktemp -d)"

cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

if [[ -z "$(find "$BOOKS" -type f ! -name .gitkeep -print -quit 2>/dev/null)" ]]; then
  echo "Error: missing or empty $BOOKS" >&2
  exit 1
fi

mkdir -p "$STAGE/resources/books" "$STAGE/resources/members"
cp -a "$BOOKS/." "$STAGE/resources/books/"
if [[ -d "$MEMBERS" ]]; then
  cp -a "$MEMBERS/." "$STAGE/resources/members/"
fi
find "$STAGE" -name .gitkeep -delete

mkdir -p "$OUT_DIR"
rm -f "$ZIP"
(
  cd "$STAGE"
  zip -qr "$ZIP" resources
)

size="$(du -h "$ZIP" | cut -f1)"
echo "Created $ZIP ($size)"
echo "Upload it as an asset on the installer data release."
