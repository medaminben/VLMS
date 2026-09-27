#!/usr/bin/env bash
# Trigger the GitHub Actions Windows installer build from Linux and download the .exe.
#
# Prerequisites:
#   gh auth login
#   git remote pointing to GitHub
#
# Usage:
#   ./scripts/build_windows_installer_from_linux.sh              # dispatch only
#   ./scripts/build_windows_installer_from_linux.sh --push       # push branch, then dispatch
#   ./scripts/build_windows_installer_from_linux.sh --watch 123  # watch an existing run

set -euo pipefail

echo "==> VLMS Windows installer build"
echo ""

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

push_changes=false
watch_run=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --push)
      push_changes=true
      shift
      ;;
    --watch)
      watch_run="${2:-}"
      if [[ -z "$watch_run" ]]; then
        echo "Error: --watch requires a run id." >&2
        exit 1
      fi
      shift 2
      ;;
    --watch=*)
      watch_run="${1#*=}"
      shift
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

if ! command -v gh >/dev/null 2>&1; then
  echo "Error: GitHub CLI (gh) is required."
  echo "Install: https://cli.github.com/  then run: gh auth login"
  exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "Error: not logged in to GitHub. Run: gh auth login"
  exit 1
fi

remote_url="$(git remote get-url origin 2>/dev/null || true)"
if [[ -z "$remote_url" ]]; then
  echo "Error: no git remote 'origin'."
  exit 1
fi

branch="$(git rev-parse --abbrev-ref HEAD)"
head_sha="$(git rev-parse HEAD)"

if $push_changes; then
  echo "Pushing $branch to origin..."
  git push -u origin "$branch"
fi

if [[ -n "$watch_run" ]]; then
  run_id="$watch_run"
else
  run_id=""
  existing_id=""
  existing_sha=""
  while IFS=$'\t' read -r existing_id existing_sha; do
    [[ -n "$existing_id" ]] || continue
    break
  done < <(
    gh run list \
      --workflow=windows-installer.yml \
      --branch "$branch" \
      --limit 5 \
      --json databaseId,headSha,status \
      --jq '.[] | select(.status=="in_progress" or .status=="queued") | "\(.databaseId)\t\(.headSha)"' \
      2>/dev/null | head -1
  )

  if [[ -n "$existing_id" && "$existing_sha" == "$head_sha" ]]; then
    echo "Reusing in-progress run $existing_id for current commit."
    run_id="$existing_id"
  else
    if [[ -n "$existing_id" ]]; then
      echo "A build is already running ($existing_id) for another commit."
      echo "GitHub will cancel it when this new run starts (concurrency limit = 1)."
    fi
    echo "Starting Windows installer workflow..."
    gh workflow run windows-installer.yml --ref "$branch"
    sleep 5
    for _ in $(seq 1 30); do
      run_id="$(gh run list --workflow=windows-installer.yml --branch "$branch" --limit 1 --json databaseId,status --jq '.[0] | select(.status=="queued" or .status=="in_progress") | .databaseId' 2>/dev/null || true)"
      if [[ -n "$run_id" && "$run_id" != "null" ]]; then
        break
      fi
      sleep 2
    done
  fi

  if [[ -z "$run_id" || "$run_id" == "null" ]]; then
    echo "Could not find workflow run. Check: gh run list --workflow=windows-installer.yml"
    exit 1
  fi
fi

repo_slug="$(gh repo view --json nameWithOwner -q .nameWithOwner)"
echo ""
echo "Run: https://github.com/${repo_slug}/actions/runs/${run_id}"
echo "Watching run $run_id..."
echo "  First OCR build: ~10–20 min (cached afterwards: ~5–10 min)"
echo "  Only one run at a time; starting again cancels the previous run."
echo ""
gh run watch "$run_id" --exit-status

mkdir -p dist
rm -rf dist/VLMS_Setup_download
gh run download "$run_id" --name VLMS_Setup --dir dist/VLMS_Setup_download

installer="$(find dist/VLMS_Setup_download -name 'VLMS_Setup_*.exe' -print -quit)"
if [[ -z "$installer" ]]; then
  echo "Error: installer .exe not found in artifact."
  exit 1
fi

dest="dist/$(basename "$installer")"
mv -f "$installer" "$dest"
rmdir dist/VLMS_Setup_download 2>/dev/null || rm -rf dist/VLMS_Setup_download

echo ""
echo "=== Installer ready ==="
echo "$dest"
ls -lh "$dest"
