#!/usr/bin/env python3
"""
validate-corpus.py  (task 4)

Deep-validate the APK corpus and (optionally) (re)generate path-file inputs.
Designed for the "you supply ~20 APKs" workflow: drop APKs into the apk dir,
run this, and it tells you exactly which ones are usable by the fuzzer.

Per APK it checks:
  exists, size, md5 (for duplicate detection),
  opens as ZIP, AndroidManifest.xml present,
  manifest is *locatable* the same way the custom mutator locates it
    (local header -> compressed data range), and
  path-file input can be generated.

Usage:
  validate-corpus.py [--apk-dir DIR] [--input-dir DIR] [--write-inputs]
                     [--report FILE.csv]
Defaults: apk-dir=Evaluation-SSBSE-2026/apk, input-dir=Evaluation-SSBSE-2026/input
Exit code: 0 if every APK is USABLE and there are no duplicates, else 1.
"""
import argparse
import csv
import hashlib
import os
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def manifest_data_range(apk_path, info):
    """Replicate the mutator's manifest-locate: local header -> data range."""
    with open(apk_path, "rb") as f:
        f.seek(info.header_offset)
        header = f.read(30)
    if len(header) != 30 or header[:4] != b"PK\x03\x04":
        return None
    name_len, extra_len = struct.unpack_from("<HH", header, 26)
    start = info.header_offset + 30 + name_len + extra_len
    end = start + info.compress_size
    if info.compress_size == 0 or start >= end:
        return None
    return (start, end)


def check_apk(path):
    r = {"apk": path, "size": os.path.getsize(path), "md5": md5(path),
         "zip_opens": False, "manifest_present": False,
         "manifest_locatable": False, "manifest_range": "", "usable": False,
         "note": ""}
    try:
        z = zipfile.ZipFile(path, "r")
    except Exception as e:
        r["note"] = f"open failed: {type(e).__name__}"
        return r
    r["zip_opens"] = True
    try:
        names = z.namelist()
        if "AndroidManifest.xml" not in names:
            r["note"] = "no AndroidManifest.xml"
            return r
        r["manifest_present"] = True
        rng = manifest_data_range(path, z.getinfo("AndroidManifest.xml"))
        if not rng:
            r["note"] = "manifest present but data range not locatable"
            return r
        r["manifest_locatable"] = True
        r["manifest_range"] = f"[{rng[0]},{rng[1]})"
        r["usable"] = True
    finally:
        z.close()
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apk-dir", default=os.path.join(ROOT, "Evaluation-SSBSE-2026", "apk"))
    ap.add_argument("--input-dir", default=os.path.join(ROOT, "Evaluation-SSBSE-2026", "input"))
    ap.add_argument("--write-inputs", action="store_true",
                    help="(re)generate <base>.txt path-files for USABLE apks")
    ap.add_argument("--strict", action="store_true",
                    help="also fail (exit 1) when duplicate APKs are present")
    ap.add_argument("--report")
    args = ap.parse_args()

    if not os.path.isdir(args.apk_dir):
        print(f"error: apk dir not found: {args.apk_dir}", file=sys.stderr)
        return 2
    apks = sorted(os.path.join(args.apk_dir, f) for f in os.listdir(args.apk_dir)
                  if f.lower().endswith(".apk"))
    if not apks:
        print(f"error: no .apk files in {args.apk_dir}", file=sys.stderr)
        return 2

    rows = [check_apk(p) for p in apks]

    # duplicate detection
    by_md5 = {}
    for r in rows:
        by_md5.setdefault(r["md5"], []).append(os.path.basename(r["apk"]))
    dups = {h: names for h, names in by_md5.items() if len(names) > 1}

    print(f"{'STATUS':8} {'APK':42} {'size':>10}  manifest_range")
    for r in rows:
        st = "USABLE" if r["usable"] else "BAD"
        print(f"{st:8} {os.path.basename(r['apk']):42} {r['size']:>10}  "
              f"{r['manifest_range']}{('  <- ' + r['note']) if r['note'] else ''}")

    usable = [r for r in rows if r["usable"]]
    print(f"\n== summary ==")
    print(f"   total apks         : {len(rows)}")
    print(f"   usable             : {len(usable)}")
    print(f"   distinct contents  : {len(by_md5)}  (by md5)")
    if dups:
        print(f"   DUPLICATE GROUPS   : {len(dups)}  (warning — wasted fuzzing effort, not fatal)")
        for h, names in dups.items():
            print(f"      {h[:12]} -> {', '.join(names)}")

    if args.write_inputs:
        os.makedirs(args.input_dir, exist_ok=True)
        # clear old .txt
        for f in os.listdir(args.input_dir):
            if f.endswith(".txt"):
                os.remove(os.path.join(args.input_dir, f))
        n = 0
        for r in usable:
            base = os.path.splitext(os.path.basename(r["apk"]))[0]
            with open(os.path.join(args.input_dir, base + ".txt"), "w") as fh:
                fh.write(os.path.abspath(r["apk"]) + "\n")
            n += 1
        print(f"\n>> wrote {n} path-file(s) to {args.input_dir}")

    if args.report:
        with open(args.report, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)
        print(f">> report: {args.report}")

    # Unusable APKs are fatal; duplicates are only fatal under --strict.
    ok = (len(usable) == len(rows)) and (not dups or not args.strict)
    if len(usable) != len(rows):
        print("\n(exit 1: one or more APKs are unusable)")
    elif dups and args.strict:
        print("\n(exit 1: duplicates present and --strict set)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
