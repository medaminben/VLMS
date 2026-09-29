#!/usr/bin/env bash
# Line coverage for the whole tree: configure with VLMS_COVERAGE, build, run
# ctest, then report with gcovr (text summary, HTML and Cobertura XML).
#
#   scripts/coverage.sh [build-dir]                 default: build-coverage
#   scripts/coverage.sh <build-dir> --report-only   CI: the build already ran the tests
#
# Needs gcovr on PATH (apt install gcovr, or pip install gcovr).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build-coverage}"
REPORT="$BUILD/coverage"

command -v gcovr >/dev/null || { echo "gcovr not found (apt install gcovr)"; exit 1; }

if [[ "${2:-}" != "--report-only" ]]; then
    cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug -DVLMS_BUILD_TESTS=ON -DVLMS_COVERAGE=ON
    # A bare --parallel is an unbounded make -j; -O0 --coverage compiles exhaust RAM.
    cmake --build "$BUILD" --parallel "${JOBS:-4}"
    find "$BUILD" -name '*.gcda' -delete
    # Coverage counts what ran; timings under -O0 --coverage are not meaningful.
    (cd "$BUILD" && QT_QPA_PLATFORM=offscreen VLMS_SKIP_BENCH=1 ctest --output-on-failure -LE realdb)
fi

mkdir -p "$REPORT"
# Production sources only: tests, generated moc/qrc and third-party code
# would inflate the number.
gcovr --root "$ROOT" --object-directory "$BUILD" \
    --filter "$ROOT/libraries/.*/src/" --filter "$ROOT/libraries/.*/include/" \
    --filter "$ROOT/applications/vlms/src/" \
    --exclude '.*_autogen/.*' --exclude '.*/test/.*' \
    --print-summary \
    --txt "$REPORT/summary.txt" \
    --html-details "$REPORT/index.html" \
    --cobertura "$REPORT/coverage.xml"
echo "Report: $REPORT/index.html"
