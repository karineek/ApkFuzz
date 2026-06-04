while true; do
  # 1. Get a list of all APKs in the seeds folder into an array
  shopt -s nullglob
  FILES=(seeds/*.apk)
  
  # 2. Check if the folder is empty
  if [ ${#FILES[@]} -eq 0 ]; then
    echo "No APKs found in seeds/ folder. Exiting."
    break
  fi

  # 3. Pick a random index and copy that file to F-Droid1.apk
  RANDOM_FILE="${FILES[$((RANDOM % ${#FILES[@]}))]}"
  echo "Testing: $RANDOM_FILE"
  cp "$RANDOM_FILE" F-Droid1.apk

  # 4. Run your test
  python3 src/blackbox-onerun.py

  # 5. Check results
  if unzip -t F-Droid1.apk >/dev/null; then
    echo "ZIP OK"
    apkanalyzer apk summary F-Droid1.apk
    
    # Save the successful result with a unique hashed name
    HASH=$(date +%s%N | md5sum | head -c 8)
    cp F-Droid1.apk "seeds/F-Droid_${HASH}.apk"
  else
    echo "ZIP BROKEN"
  fi
  
  echo ">>>>>>>>>>>>>>>>>>>>"
done

