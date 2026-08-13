#!/usr/bin/env bash
set -euo pipefail

DURATION="${1:-10}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
INPUT_DIR="$PROJECT_ROOT/Evaluation-SSBSE-2026/input"
APK_DIR="$PROJECT_ROOT/Evaluation-SSBSE-2026/apk"
OUTPUT_DIR="$PROJECT_ROOT/afl-custom-smoke-output"
MUTATOR="$PROJECT_ROOT/build/cm-ApkVulFuzz.so"
HARNESS="$PROJECT_ROOT/afl/apk_pathfile_harness.py"
PYTHON="$PROJECT_ROOT/.venv/bin/python3"
RUN_MARKER="$(mktemp)"

cleanup() {
  if [ -d "$APK_DIR" ]; then
    find "$APK_DIR" -maxdepth 1 -regextype posix-extended -type f \
      -name '*.apk' -newer "$RUN_MARKER" \
      -regex '.*/.*_[0-9]+_[0-9]+\.apk' -delete
  fi
  rm -f "$RUN_MARKER"
}
trap cleanup EXIT

if [ ! -x "$PYTHON" ]; then
  PYTHON="$(command -v python3)"
fi

if ! command -v afl-fuzz >/dev/null 2>&1; then
  echo "error: afl-fuzz not found in PATH" >&2
  exit 1
fi

if [ ! -d "$INPUT_DIR" ]; then
  echo "error: path-file input directory not found: $INPUT_DIR" >&2
  exit 1
fi

if [ ! -d "$APK_DIR" ]; then
  echo "error: APK directory not found: $APK_DIR" >&2
  exit 1
fi

if [ ! -f "$MUTATOR" ]; then
  echo "error: custom mutator not found: $MUTATOR" >&2
  echo "       run: cd $PROJECT_ROOT/src && bash compile_afl_mutators.sh" >&2
  exit 1
fi

unset AFL_SKIP_CHECKS
export AFL_NO_UI=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_SKIP_CPUFREQ=1
export AFL_SKIP_BIN_CHECK=1
export AFL_CUSTOM_MUTATOR_ONLY=1
export AFL_CUSTOM_MUTATOR_LIBRARY="$MUTATOR"

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
