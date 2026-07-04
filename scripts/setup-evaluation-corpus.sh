#!/usr/bin/env bash
set -euo pipefail

CORPUS_URL="https://github.com/karineek/ApkFuzz/raw/main/Evaluation-SSBSE-2026/seeds-10March2026_apk-0.tar.gz"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EVAL_DIR="$PROJECT_ROOT/Evaluation-SSBSE-2026"
APK_DIR="$EVAL_DIR/apk"
INPUT_DIR="$EVAL_DIR/input"
LOCAL_TARBALL="$EVAL_DIR/seeds-10March2026_apk-0.tar.gz"
DOWNLOAD=0
TARBALL=""

usage() {
  echo "Usage: $0 [--download] [--tarball PATH]"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --download)
      DOWNLOAD=1
      shift
      ;;
    --tarball)
      if [ "$#" -lt 2 ]; then
        usage
        exit 2
      fi
      TARBALL="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage
      exit 2
      ;;
  esac
done

mkdir -p "$APK_DIR" "$INPUT_DIR"

if [ "$DOWNLOAD" -eq 1 ]; then
  echo ">> Downloading corpus tarball to $LOCAL_TARBALL"
  if command -v curl >/dev/null 2>&1; then
    curl -L --fail "$CORPUS_URL" -o "$LOCAL_TARBALL"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$LOCAL_TARBALL" "$CORPUS_URL"
  else
    echo "error: neither curl nor wget is installed; cannot download corpus tarball." >&2
    exit 1
  fi
fi

if [ -z "$TARBALL" ] && [ -f "$LOCAL_TARBALL" ]; then
  TARBALL="$LOCAL_TARBALL"
fi

if [ -n "$TARBALL" ]; then
  if [ ! -f "$TARBALL" ]; then
    echo "error: corpus tarball not found: $TARBALL" >&2
    exit 1
  fi

  TMP_DIR="$(mktemp -d)"
  cleanup() {
    rm -rf "$TMP_DIR"
  }
  trap cleanup EXIT

  echo ">> Extracting $TARBALL"
  tar -xzf "$TARBALL" -C "$TMP_DIR"

  while IFS= read -r -d '' apk; do
    cp -f "$apk" "$APK_DIR/$(basename "$apk")"
  done < <(find "$TMP_DIR" -type f -iname '*.apk' -print0)
else
  echo ">> No corpus tarball supplied or found; using APKs already in $APK_DIR"
  echo ">> To download the professor corpus, rerun with --download"
fi

find "$INPUT_DIR" -maxdepth 1 -type f -name '*.txt' -delete

apk_count=0
input_count=0
while IFS= read -r -d '' apk; do
  apk_count=$((apk_count + 1))
  apk_abs="$(realpath "$apk")"
  base="$(basename "$apk" .apk)"
  printf '%s\n' "$apk_abs" > "$INPUT_DIR/$base.txt"
  input_count=$((input_count + 1))
done < <(find "$APK_DIR" -maxdepth 1 -type f -iname '*.apk' -print0 | sort -z)

broken_count=0
while IFS= read -r -d '' pathfile; do
  apk_path="$(tr -d '\000\r\n' < "$pathfile")"
  if [ ! -f "$apk_path" ] || [ "${apk_path##*.}" != "apk" ]; then
    echo "broken: $pathfile -> $apk_path"
    broken_count=$((broken_count + 1))
  fi
done < <(find "$INPUT_DIR" -maxdepth 1 -type f -name '*.txt' -print0 | sort -z)

echo ">> Corpus summary"
echo "   APKs found: $apk_count"
echo "   input .txt files generated: $input_count"
echo "   broken paths: $broken_count"
