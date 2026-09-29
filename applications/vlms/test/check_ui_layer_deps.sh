#!/usr/bin/env bash
# Layer dependency lint for the VLMS UI, registered as the `ui_layer_deps` test.
# Run from anywhere; resolves paths relative to this script.
#
# Rules (spec: docs/superpowers/specs/2026-09-29-frontend-redesign-design.md):
#   1. presentation must not #include "ui/views/..." or "ui/shell/...".
#   2. views must not hold a repository (no `XxxRepository&`). Static helpers
#      such as MemberRepository::isValidEmail stay allowed.
#   3. views must not call repository-style write methods via ->verb(.
#   4. workflows must not #include "ui/views/...", except the Qt adapters
#      (QtDialogFactory, QtErrorPresenter) that implement the interfaces.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VLMS_APP="$(cd "$SCRIPT_DIR/.." && pwd)"
PRESENTATION="$VLMS_APP/src/ui/presentation"
VIEWS="$VLMS_APP/src/ui/views"
WORKFLOWS="$VLMS_APP/src/ui/workflows"
failed=0

report() {
  local title="$1" hits="$2"
  echo "== $title =="
  if [[ -n "$hits" ]]; then
    echo "$hits"
    echo "FAIL"
    failed=1
  else
    echo "PASS"
  fi
}

report "presentation must not include views or shell" \
  "$(grep -RnE '#include "ui/(views|shell)/' "$PRESENTATION" || true)"

report "views must not hold a repository" \
  "$(grep -RnE '[[:alnum:]_]+Repository[[:space:]]*&' "$VIEWS" || true)"

# Arrow-call write verbs. Excludes Qt noise (deleteLater, painter->save/restore).
# Use -E and a pattern that does not start with '-', which some greps treat as options.
write_hits="$(grep -RnE '[^[:alnum:]_](save|create|update|delete|archive|restore|purge|returnLoan|extendLoan)[[:alnum:]_]*[[:space:]]*\(' "$VIEWS" \
  | grep -F -- '->' \
  | grep -Ev -- 'deleteLater|update\(\)|painter->|viewport\(\)->' || true)"
report "views must not call repository write methods" "$write_hits"

report "workflows must not include views (Qt adapters excepted)" \
  "$(grep -RnE '#include "ui/views/' "$WORKFLOWS" | grep -Ev '/Qt[[:alnum:]_]+\.(cpp|h):' || true)"

if [[ "$failed" -ne 0 ]]; then
  exit 1
fi
echo "All layer rules PASS"
exit 0
