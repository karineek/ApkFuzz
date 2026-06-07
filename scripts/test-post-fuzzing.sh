#!/usr/bin/env bash

set +e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DROIDBOT_DIR=$1  ## e.g. "/users/kevenmen/droidbot/"
SEEDS_DIR=$2     ## e.g."/users/kevenmen/ApkVulFuzz/Evaluation-SSBSE-2026/seeds"

if [ ! -d "$SEEDS_DIR" ]; then
  echo "error: seeds directory does not exist: $SEEDS_DIR"
  exit 1
fi

for apk in "$SEEDS_DIR"/*.apk; do
  if [ ! -f "$apk" ]; then
    continue
  fi
  echo "=== Testing: $apk ==="
  bash "$SCRIPT_DIR/test-APK.sh" "$DROIDBOT_DIR" "$apk"
  status=$?
  if [ $status -ne 0 ]; then
    echo "*** APK failed: $apk (status=$status)"
  else
    echo "*** APK passed: $apk"
  fi
  echo
done
