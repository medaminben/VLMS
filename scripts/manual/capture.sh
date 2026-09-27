#!/usr/bin/env bash
# Rebuild the manual's screenshots in all three languages, offscreen, from a scrubbed
# sandbox. Never opens the live database for writing and never touches the desktop.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
build="$repo/build-manual"
sandbox="$repo/build-manual-sandbox"

cmake -S "$repo" -B "$build" -DCMAKE_BUILD_TYPE=Release \
      -DVLMS_MANUAL_CAPTURE=ON -DVLMS_BUILD_TESTS=OFF >/dev/null
cmake --build "$build" --target manual_capture -j"$(nproc)"

python3 "$repo/scripts/manual/prepare_sandbox.py" \
    --source "$repo/database/vlms.db" --books "$repo/resources/books" --out "$sandbox"

bin="$(find "$build" -type f -name manual_capture -perm -u+x | head -n1)"
status=0
for lang in ar en fr; do
    # A fresh copy per language: shots write to the sandbox (a checkout, a renewal),
    # and each language must start from the same data.
    python3 "$repo/scripts/manual/prepare_sandbox.py" \
        --source "$repo/database/vlms.db" --books "$repo/resources/books" --out "$sandbox" >/dev/null
    QT_QPA_PLATFORM=offscreen VLMS_SCHEMA_PATH="$repo/database/schema.sql" \
        "$bin" --sandbox "$sandbox" --out "$repo/docs/manual/assets" --lang "$lang" "$@" || status=$?
done
exit "$status"
