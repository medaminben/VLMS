#!/usr/bin/env bash
# Layer dependency lint for VLMS UI (Phase 5 Task 5).
# Run from anywhere; resolves paths relative to this script.
#
# Rules:
#   1. presentation must not #include "ui/views/..."
#   2. views must not call repository-style write methods via ->verb(
#      CategoryManagerDialog category CRUD via m_repository.* is an allowed
#      Phase 4 exception (document-only; the arrow pattern usually misses it).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VLMS_APP="$(cd "$SCRIPT_DIR/.." && pwd)"
PRESENTATION="$VLMS_APP/src/ui/presentation"
VIEWS="$VLMS_APP/src/ui/views"
failed=0

echo "== presentation must not include ui/views =="
pres_hits="$(grep -Rn '#include "ui/views' "$PRESENTATION" || true)"
if [[ -n "$pres_hits" ]]; then
  echo "$pres_hits"
  echo "FAIL: presentation includes views"
  failed=1
else
  echo "PASS"
fi

echo "== views must not call repository write methods via -> =="
# Arrow-call write verbs. Excludes Qt noise (deleteLater, painter->save/restore).
# Use -E and a pattern that does not start with '-', which some greps treat as options.
write_hits="$(grep -RnE '[^[:alnum:]_](save|create|update|delete|archive|restore|purge|returnLoan|extendLoan)[[:alnum:]_]*[[:space:]]*\(' "$VIEWS" \
  | grep -F -- '->' || true)"
filtered="$(printf '%s\n' "$write_hits" \
  | grep -v 'CategoryManagerDialog' \
  | grep -Ev -- 'deleteLater|update\(\)|painter->|viewport\(\)->' \
  | sed '/^$/d' || true)"

if [[ -n "$filtered" ]]; then
  echo "$filtered"
  echo "FAIL: views call repository write methods"
  failed=1
else
  # Explicit note when CategoryManagerDialog still does m_repository.* writes
  if grep -qnE '\.(create|update|delete)Category\s*\(' \
      "$VIEWS/CategoryManagerDialog.cpp" 2>/dev/null; then
    echo "PASS (allowed exception: CategoryManagerDialog → CatalogRepository category CRUD)"
  else
    echo "PASS"
  fi
fi

if [[ "$failed" -ne 0 ]]; then
  exit 1
fi
echo "All layer include/write greps PASS"
exit 0
