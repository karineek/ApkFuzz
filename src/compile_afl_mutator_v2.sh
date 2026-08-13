#!/usr/bin/env bash
# Compile the multi-strategy (v2) AFL++ custom mutator into build/cm-ApkVulFuzz2.so
# This is SEPARATE from compile_afl_mutators.sh; it never touches the baseline
# mutator sources (cm_ApkVulFuzz.c / bitflip.c / apk.c).
set -euo pipefail

AFLPP_DIR="${AFLPP_DIR:-$HOME/AFLplusplus}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"          # .../src
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MUTATOR_DIR="$SCRIPT_DIR/custom_mutators/v2"
BUILD_DIR="$PROJECT_ROOT/build"

if [ ! -f "$AFLPP_DIR/include/afl-fuzz.h" ] || [ ! -f "$AFLPP_DIR/src/afl-performance.c" ]; then
  echo "error: AFL++ source not found under AFLPP_DIR=$AFLPP_DIR" >&2
  echo "       set AFLPP_DIR=/path/to/AFLplusplus" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR"
cd "$MUTATOR_DIR"

CC="${CC:-clang}"
CFLAGS=(-fPIC -Wall -O2 -I"$AFLPP_DIR/include" -I. -D AFL_CM)

"$CC" -c "${CFLAGS[@]}" zip2.c            -o zip2.o
"$CC" -c "${CFLAGS[@]}" strategies.c      -o strategies.o
"$CC" -c "${CFLAGS[@]}" cm_ApkVulFuzz2.c  -o cm_ApkVulFuzz2.o
"$CC" -c -fPIC -Wall -O2 -I"$AFLPP_DIR/include" \
      "$AFLPP_DIR/src/afl-performance.c" -o afl-performance.o

"$CC" -shared -o cm-ApkVulFuzz2.so \
      afl-performance.o zip2.o strategies.o cm_ApkVulFuzz2.o

cp -f cm-ApkVulFuzz2.so "$BUILD_DIR/"
echo ">> Built $BUILD_DIR/cm-ApkVulFuzz2.so"
nm -D "$BUILD_DIR/cm-ApkVulFuzz2.so" | grep afl_custom || {
  echo "error: expected afl_custom_* symbols missing" >&2; exit 1; }

# Optional: build a standalone tester (no AFL) for offline mode checks.
"$CC" -Wall -O2 -D APKFUZZ_STANDALONE zip2.c strategies.c cm_ApkVulFuzz2.c \
      -o "$BUILD_DIR/mut2-standalone"
echo ">> Built $BUILD_DIR/mut2-standalone  (usage: APKFUZZ_MUTATOR_MODE=... ./mut2-standalone /path/to.apk)"
