#!/usr/bin/env bash
set -euo pipefail

DURATION="${1:-10}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INPUT_DIR="$PROJECT_ROOT/Evaluation-SSBSE-2026/apk"
OUTPUT_DIR="$PROJECT_ROOT/afl-smoke-output"
HARNESS="$PROJECT_ROOT/afl/apk_zip_harness.py"
PYTHON="$PROJECT_ROOT/.venv/bin/python3"

if [ ! -x "$PYTHON" ]; then
  PYTHON="$(command -v python3)"
fi

if ! command -v afl-fuzz >/dev/null 2>&1; then
  echo "error: afl-fuzz not found in PATH" >&2
  exit 1
fi

if [ ! -d "$INPUT_DIR" ]; then
  echo "error: APK input directory not found: $INPUT_DIR" >&2
  exit 1
fi

unset AFL_SKIP_CHECKS
export AFL_NO_UI=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_SKIP_CPUFREQ=1
export AFL_SKIP_BIN_CHECK=1

cd "$PROJECT_ROOT"
rm -rf "$OUTPUT_DIR"
afl-fuzz -V "$DURATION" -m none -i "$INPUT_DIR" -o "$OUTPUT_DIR" -- \
  "$PYTHON" "$HARNESS" @@

STATS="$OUTPUT_DIR/default/fuzzer_stats"
if [ -f "$STATS" ]; then
  echo ">> First lines from $STATS"
  sed -n '1,40p' "$STATS"
else
  echo "warning: fuzzer_stats not found: $STATS" >&2
fi
