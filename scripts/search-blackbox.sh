#!/usr/bin/env bash

# This script runs the blackbox APK fuzzer across one or more seed APKs.
# Usage examples:
#   ./scripts/search-blackbox.sh --short               # run seeds/*.apk for 10 minutes
#   ./scripts/search-blackbox.sh --medium my-apks      # run my-apks/*.apk for 2 hours
#   ./scripts/search-blackbox.sh --long corpus/apks    # run corpus/apks/*.apk for 24 hours

MODE="$1"
SEEDS_DIR="${2:-seeds}"
WORK_APK="${TMPDIR:-/tmp}/apkvulfuzz-work-$$.apk"
trap 'rm -f "$WORK_APK"' EXIT

# Decide how long the script should run based on the flag the user gave.
case "$MODE" in
  --short|short)
    DURATION_SECONDS=$((10 * 60))
    MODE_NAME="short"
    ;;

  --medium|medium)
    DURATION_SECONDS=$((2 * 60 * 60))
    MODE_NAME="medium"
    ;;

  --long|long)
    DURATION_SECONDS=$((24 * 60 * 60))
    MODE_NAME="long"
    ;;

  *)
    echo "Usage: $0 --short | --medium | --long [seeds-dir]"
    echo ""
    echo "Modes:"
    echo "  --short    Run for 10 minutes"
    echo "  --medium   Run for 2 hours"
    echo "  --long     Run for 24 hours"
    echo ""
    echo "seeds-dir defaults to: seeds"
    exit 1
    ;;
esac

if [ ! -d "$SEEDS_DIR" ]; then
  echo "Seeds directory does not exist: $SEEDS_DIR"
  exit 1
fi

START_SECONDS=$SECONDS

# This counts how many fuzzing attempts we started.
ROUND=0

# Enable nullglob so that if there are no APK files,
# "$SEEDS_DIR"/*.apk becomes empty instead of staying as a literal glob.
shopt -s nullglob

# Save the original APK seed list once at the beginning.
# This is important because the script creates new mutated APKs inside the seed folder.
# We do not want the outer seed list to keep changing while the script is running.
ORIGINAL_SEEDS=("$SEEDS_DIR"/*.apk)

# If there are no APKs in the selected seed folder, there is nothing to fuzz.
if [ ${#ORIGINAL_SEEDS[@]} -eq 0 ]; then
  echo "No APKs found in $SEEDS_DIR. Exiting."
  exit 1
fi

echo "Starting blackbox fuzzing"
echo "Mode: $MODE_NAME"
echo "Seeds directory: $SEEDS_DIR"
echo "Duration: $DURATION_SECONDS seconds"
echo "Initial seeds found: ${#ORIGINAL_SEEDS[@]}"
echo ">>>>>>>>>>>>>>>>>>>>"

# Go over each original seed APK one by one.
# Each seed gets one mutation chain. When that chain breaks, continue to the next seed.
for SEED_APK in "${ORIGINAL_SEEDS[@]}"; do

  # If the time limit ended, stop before starting another seed.
  if (( SECONDS - START_SECONDS >= DURATION_SECONDS )); then
    break
  fi

  # Get the seed file name without the .apk ending.
  SEED_BASENAME="$(basename "$SEED_APK" .apk)"

  # CURRENT_APK is the APK we are currently mutating.
  # If we get a valid mutation, CURRENT_APK becomes that new mutated APK.
  CURRENT_APK="$SEED_APK"

  echo "Starting seed: $SEED_APK"

  # Keep mutating the current seed chain until:
  #   1. time runs out, or
  #   2. a mutation breaks the ZIP/APK structure.
  while (( SECONDS - START_SECONDS < DURATION_SECONDS )); do
    ROUND=$((ROUND + 1))
    echo "Round: $ROUND"
    echo "Testing: $CURRENT_APK"

    # Copy the current APK into a temp file before mutating it.
    if ! cp "$CURRENT_APK" "$WORK_APK"; then
      echo "Could not copy APK into work file: $CURRENT_APK"
      echo "Moving to next seed"
      echo ">>>>>>>>>>>>>>>>>>>>"
      break
    fi

    # Run one mutation.
    # This flips one random bit inside the work APK.
    if ! python3 src/blackbox-onerun.py "$WORK_APK"; then
      echo "Mutation failed"
      echo "Moving to next seed"
      echo ">>>>>>>>>>>>>>>>>>>>"
      break
    fi

    # APK files are ZIP-based files.
    # This checks if the mutated APK is still a valid ZIP/APK structure.
    if unzip -t "$WORK_APK" >/dev/null; then
      echo "ZIP OK"

      # Print basic APK information, such as package name and version.
      if ! apkanalyzer apk summary "$WORK_APK"; then
        echo "APK summary failed"
        echo "Moving to next seed"
        echo ">>>>>>>>>>>>>>>>>>>>"
        break
      fi

      # Create a short unique hash for the new mutated APK file name.
      HASH=$(date +%s%N | md5sum | head -c 8)

      # Save the valid mutated APK back into the selected seed folder.
      # The name keeps the original seed name and adds a unique hash.
      MUTATED_APK="$SEEDS_DIR/${SEED_BASENAME}_${HASH}.apk"
      if ! cp "$WORK_APK" "$MUTATED_APK"; then
        echo "Could not save valid mutation: $MUTATED_APK"
        echo "Moving to next seed"
        echo ">>>>>>>>>>>>>>>>>>>>"
        break
      fi

      echo "Saved valid mutation: $MUTATED_APK"

      # Continue mutating from the successful mutation.
      CURRENT_APK="$MUTATED_APK"
    else
      # If the ZIP/APK structure broke, this mutation is not useful.
      # Stop working on this seed and move to the next original seed.
      echo "ZIP BROKEN"
      echo "Moving to next seed"
      echo ">>>>>>>>>>>>>>>>>>>>"
      break
    fi

    echo ">>>>>>>>>>>>>>>>>>>>"
  done
done

echo "Finished blackbox fuzzing"
echo "Mode: $MODE_NAME"
echo "Rounds attempted: $ROUND"
