#!/usr/bin/env bash
set -euo pipefail

AFLPP_DIR="${AFLPP_DIR:-$HOME/AFLplusplus}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MUTATOR_DIR="$SCRIPT_DIR/custom_mutators"
BUILD_DIR="$PROJECT_ROOT/build"

if [ ! -f "$AFLPP_DIR/src/afl-performance.c" ]; then
  echo "error: AFL++ helper source not found."
  echo "       AFLPP_DIR: $AFLPP_DIR"
  echo "       expected: $AFLPP_DIR/src/afl-performance.c"
  echo "       Set AFLPP_DIR=/path/to/AFLplusplus if it is installed elsewhere."
  exit 1
fi

if [ ! -f "$AFLPP_DIR/include/afl-fuzz.h" ]; then
  echo "error: AFL++ header not found."
  echo "       AFLPP_DIR: $AFLPP_DIR"
  echo "       expected: $AFLPP_DIR/include/afl-fuzz.h"
  echo "       Set AFLPP_DIR=/path/to/AFLplusplus if it is installed elsewhere."
  exit 1
fi

mkdir -p "$BUILD_DIR"
cd "$MUTATOR_DIR" || exit 1

# Compile AFL helper
clang -c -fPIC -Wall -O3 \
  -I"$AFLPP_DIR/include" \
  "$AFLPP_DIR/src/afl-performance.c" \
  -o afl-performance.o

# Compile sources (SEPARATELY)
clang -c -fPIC -Wall -O3 \
  -I"$AFLPP_DIR/include" -I. \
  -D AFL_CM \
  bitflip.c -o bitflip.o

clang -c -fPIC -Wall -O3 \
  -I"$AFLPP_DIR/include" -I. \
  -D AFL_CM \
  apk.c -o apk.o

clang -c -fPIC -Wall -O3 \
  -I"$AFLPP_DIR/include" -I. \
  -D AFL_CM \
  cm_ApkVulFuzz.c -o cm_ApkVulFuzz.o

# Link into shared object
clang -shared -o cm-ApkVulFuzz.so \
  afl-performance.o apk.o bitflip.o cm_ApkVulFuzz.o

# Copy outputs
cp *.o "$BUILD_DIR"
cp *.so "$BUILD_DIR"

echo ">> Done."
ls -l "$BUILD_DIR"
