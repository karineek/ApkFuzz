#!/usr/bin/env bash
# run-corpus-smoke.sh  (task 5)
#
# Short smoke test over the WHOLE corpus with a selectable custom mutator/strategy,
# with per-iteration logging enabled so you can prove multi-APK cycling (task 3)
# and confirm custom-mutator-only operation (task 2) in one shot.
#
# Usage:
#   scripts/run-corpus-smoke.sh [DURATION_SECONDS] [MUTATOR_SO] [MODE]
# Examples:
#   scripts/run-corpus-smoke.sh 30
#   scripts/run-corpus-smoke.sh 30 build/cm-ApkVulFuzz2.so manifest-segment
#
# Defaults: DURATION=20, MUTATOR=build/cm-ApkVulFuzz2.so (falls back to the
# baseline build/cm-ApkVulFuzz.so if the v2 one is absent), MODE=manifest-bitflip.
set -euo pipefail

DURATION="${1:-20}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

INPUT_DIR="$PROJECT_ROOT/Evaluation-SSBSE-2026/input"
APK_DIR="$PROJECT_ROOT/Evaluation-SSBSE-2026/apk"
HARNESS="$PROJECT_ROOT/afl/apk_pathfile_harness.py"
PYTHON="$PROJECT_ROOT/.venv/bin/python3"
[ -x "$PYTHON" ] || PYTHON="$(command -v python3)"

DEFAULT_MUT="$PROJECT_ROOT/build/cm-ApkVulFuzz2.so"
[ -f "$DEFAULT_MUT" ] || DEFAULT_MUT="$PROJECT_ROOT/build/cm-ApkVulFuzz.so"
MUTATOR="${2:-$DEFAULT_MUT}"
MODE="${3:-manifest-bitflip}"

STAMP="$(date +%Y%m%d-%H%M%S)"
OUTPUT_DIR="$PROJECT_ROOT/afl-corpus-smoke-$STAMP"
LOG_FILE="$OUTPUT_DIR/apkfuzz_mutator.log"
SCRATCH_DIR="$OUTPUT_DIR/mutants-scratch"   # transient mutants live here, OUTSIDE the corpus
RUN_MARKER="$(mktemp)"

cleanup() {
  # transient mutants live in their own scratch dir -> just remove it (corpus untouched)
  rm -rf "$SCRATCH_DIR" 2>/dev/null || true
  # defensive: if a mutant ever lands in the seed dir anyway, remove only this run's
  if [ -d "$APK_DIR" ]; then
    find "$APK_DIR" -maxdepth 1 -regextype posix-extended -type f \
      -name '*.apk' -newer "$RUN_MARKER" \
      -regex '.*/.*_[0-9]+_[0-9]+\.apk' -delete 2>/dev/null || true
  fi
  rm -f "$RUN_MARKER"
}
trap cleanup EXIT

command -v afl-fuzz >/dev/null 2>&1 || { echo "error: afl-fuzz not in PATH" >&2; exit 1; }
[ -f "$MUTATOR" ] || { echo "error: mutator not found: $MUTATOR (build it first)" >&2; exit 1; }
[ -d "$INPUT_DIR" ] || { echo "error: input dir not found: $INPUT_DIR" >&2; exit 1; }

# Validate + (re)generate path-files for the current corpus first.
if [ -f "$SCRIPT_DIR/validate-corpus.py" ]; then
  echo ">> validating corpus and regenerating path-files"
  "$PYTHON" "$SCRIPT_DIR/validate-corpus.py" --write-inputs || {
    echo "error: corpus validation failed; fix the APKs above before smoke testing" >&2; exit 1; }
fi

seed_count=$(find "$INPUT_DIR" -maxdepth 1 -name '*.txt' | wc -l | tr -d ' ')
echo ">> seeds (path-files): $seed_count"
echo ">> mutator: $MUTATOR   mode: $MODE   duration: ${DURATION}s"

mkdir -p "$OUTPUT_DIR"

export AFL_NO_UI=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_SKIP_CPUFREQ=1
export AFL_SKIP_BIN_CHECK=1
export AFL_CUSTOM_MUTATOR_ONLY=1
export AFL_CUSTOM_MUTATOR_LIBRARY="$MUTATOR"
export APKFUZZ_MUTATOR_MODE="$MODE"
export APKFUZZ_LOG=1
export APKFUZZ_LOG_FILE="$LOG_FILE"

# Route transient mutants to a scratch dir OUTSIDE the corpus (auto-cleaned on exit),
# so a killed run can never pollute Evaluation-SSBSE-2026/apk with stray "seeds".
mkdir -p "$SCRATCH_DIR"
export APKFUZZ_OUT_DIR="$SCRATCH_DIR"

# Optionally keep a persistent copy of each mutant for post-run analysis.
# Normalize a relative dir to an ABSOLUTE path against the caller's cwd *now*,
# before we cd into PROJECT_ROOT — otherwise the script (caller cwd) and the
# mutator (afl's cwd = PROJECT_ROOT) would disagree on where it lives.
if [ -n "${APKFUZZ_SAVE_DIR:-}" ]; then
  case "$APKFUZZ_SAVE_DIR" in
    /*) : ;;                                     # already absolute
    *)  APKFUZZ_SAVE_DIR="$PWD/$APKFUZZ_SAVE_DIR" ;;
  esac
  mkdir -p "$APKFUZZ_SAVE_DIR"
  export APKFUZZ_SAVE_DIR
  echo ">> saving mutants to: $APKFUZZ_SAVE_DIR"
fi

cd "$PROJECT_ROOT"
# Do NOT swallow afl-fuzz's exit status: a smoke test must fail loudly.
afl_rc=0
afl-fuzz -V "$DURATION" -m none -i "$INPUT_DIR" -o "$OUTPUT_DIR" -- \
  "$PYTHON" "$HARNESS" @@ || afl_rc=$?

echo
echo "================ smoke summary ================"
STATS="$OUTPUT_DIR/default/fuzzer_stats"
if [ ! -f "$STATS" ]; then
  echo "ERROR: afl-fuzz produced no fuzzer_stats (exit=$afl_rc)." >&2
  echo "       The run aborted during setup — likely a missing/bad mutator, invalid" >&2
  echo "       seeds, or a harness crash. Smoke test FAILED." >&2
  exit 1
fi
grep -E 'execs_done|execs_per_sec|corpus_count|saved_crashes|saved_hangs|cycles_done' "$STATS"

echo
echo "-- multi-APK evidence (distinct src= in mutator log) --"
if [ -f "$LOG_FILE" ]; then
  grep -oE 'src=[^ ]+' "$LOG_FILE" | sort -u
  echo "distinct seeds mutated: $(grep -oE 'src=[^ ]+' "$LOG_FILE" | sort -u | wc -l | tr -d ' ')"
  echo "mutator log: $LOG_FILE"
else
  echo "(no mutator log; APKFUZZ_LOG may not have been honored — check the mutator build)"
fi
echo "output dir: $OUTPUT_DIR"

if [ "$afl_rc" -ne 0 ]; then
  echo "ERROR: afl-fuzz exited non-zero (exit=$afl_rc). Smoke test FAILED." >&2
  exit "$afl_rc"
fi
