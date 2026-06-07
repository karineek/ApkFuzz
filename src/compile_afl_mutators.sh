#!/usr/bin/env bash
set -euo pipefail

mkdir -p ../build
cd custom_mutators/ || exit 1

if [ ! -f "$HOME/AFLplusplus/src/afl-performance.c" ] || [ ! -f "$HOME/AFLplusplus/include/afl-fuzz.h" ]; then
  echo "error: AFLplusplus was not found under $HOME/AFLplusplus"
  echo "       expected: $HOME/AFLplusplus/src/afl-performance.c"
  echo "       expected: $HOME/AFLplusplus/include/afl-fuzz.h"
  exit 1
fi

# Compile AFL helper
clang -c -fPIC -Wall -O3 \
  -I"$HOME/AFLplusplus/include" \
  "$HOME/AFLplusplus/src/afl-performance.c" \
  -o afl-performance.o

# Compile sources (SEPARATELY)
clang -c -fPIC -Wall -O3 \
  -I"$HOME/AFLplusplus/include" -I. \
  -D AFL_CM \
  bitflip.c -o bitflip.o

clang -c -fPIC -Wall -O3 \
  -I"$HOME/AFLplusplus/include" -I. \
  -D AFL_CM \
  apk.c -o apk.o

clang -c -fPIC -Wall -O3 \
  -I"$HOME/AFLplusplus/include" -I. \
  -D AFL_CM \
  cm_ApkVulFuzz.c -o cm_ApkVulFuzz.o

# Link into shared object
clang -shared -o cm-ApkVulFuzz.so \
  afl-performance.o apk.o bitflip.o cm_ApkVulFuzz.o

# Copy outputs
cp *.o ../../build
cp *.so ../../build

echo ">> Done."
ls -l ../../build/
