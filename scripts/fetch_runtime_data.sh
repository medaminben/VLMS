#!/usr/bin/env bash
# Download the sample cover images and member photographs into resources/.
#
# They are not tracked. They live in VLMS_installer_data.zip on the release
# named by DATA_TAG; scripts/package_installer_data.sh builds that zip.
#
# Usage: scripts/fetch_runtime_data.sh [--force]
#   --force  download again even when resources/books/ already has covers
#
# VLMS_INSTALLER_DATA_URL overrides the download URL.

set -euo pipefail

# Keep in step with DataTag in fetch_installer_data.ps1.
DATA_TAG="installer-data-1"
ZIP_NAME="VLMS_installer_data.zip"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BOOKS="$ROOT/resources/books"

has_covers() {
  [[ -n "$(find "$BOOKS" -type f ! -name .gitkeep -print -quit 2>/dev/null)" ]]
}

if [[ "${1:-}" != "--force" ]] && has_covers; then
  echo "Cover images already present in resources/books/."
  exit 0
fi

url="${VLMS_INSTALLER_DATA_URL:-}"
if [[ -z "$url" ]]; then
  remote="$(git -C "$ROOT" remote get-url origin)"
  if [[ ! "$remote" =~ github\.com[:/]([^/]+)/([^/]+)$ ]]; then
    echo "Error: could not determine the GitHub repository from '$remote'." >&2
    exit 1
  fi
  slug="${BASH_REMATCH[1]}/${BASH_REMATCH[2]%.git}"
  url="https://github.com/$slug/releases/download/$DATA_TAG/$ZIP_NAME"
fi

STAGE="$(mktemp -d)"
cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

echo "Downloading $url"
curl -fL --progress-bar -o "$STAGE/$ZIP_NAME" "$url"
python3 -m zipfile -e "$STAGE/$ZIP_NAME" "$STAGE/out"

for subdir in books members; do
  if [[ -d "$STAGE/out/resources/$subdir" ]]; then
    mkdir -p "$ROOT/resources/$subdir"
    cp -a "$STAGE/out/resources/$subdir/." "$ROOT/resources/$subdir/"
    count="$(find "$ROOT/resources/$subdir" -type f ! -name .gitkeep | wc -l)"
    echo "  resources/$subdir/ ($count files)"
  fi
done

has_covers || { echo "Error: the zip held no cover images." >&2; exit 1; }
