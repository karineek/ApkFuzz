#!/usr/bin/env python3
"""
triage-afl-output.py  (task 7)

Summarize an AFL++ output directory after a run: key stats, crashes, hangs,
coverage progression, seed/queue diversity, and (for the path-file custom-mutator
workflow) it resolves each crashing/hanging input back to the real mutant APK it
points at.

Usage:
  triage-afl-output.py OUTDIR [--collect DIR]
    OUTDIR is an afl-fuzz -o directory (it will use OUTDIR/default if present).
    --collect DIR  copy crash/hang inputs (+ any resolved mutant APKs) into DIR.
"""
import argparse
import os
import re
import shutil
import sys


def read_stats(path):
    stats = {}
    if not os.path.isfile(path):
        return stats
    with open(path, errors="ignore") as f:
        for line in f:
            if ":" in line:
                k, _, v = line.partition(":")
                stats[k.strip()] = v.strip()
    return stats


def list_inputs(d):
    if not os.path.isdir(d):
        return []
    return [os.path.join(d, f) for f in sorted(os.listdir(d))
            if f != "README.txt" and os.path.isfile(os.path.join(d, f))]


def resolve_pathfile(p):
    """If input is a small text file containing an APK path, return it."""
    try:
        if os.path.getsize(p) > 4096:
            return None
        raw = open(p, "rb").read()
        text = raw.replace(b"\x00", b"").decode("utf-8", "ignore").strip()
        if text.lower().endswith(".apk"):
            return text
    except Exception:
        return None
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("--collect")
    args = ap.parse_args()

    base = args.outdir
    if os.path.isdir(os.path.join(base, "default")):
        base = os.path.join(base, "default")
    if not os.path.isdir(base):
        print(f"error: not a directory: {base}", file=sys.stderr)
        return 2

    stats = read_stats(os.path.join(base, "fuzzer_stats"))
    print(f"== AFL output triage: {base} ==\n")

    keys = ["afl_version", "command_line", "run_time", "cycles_done",
            "execs_done", "execs_per_sec", "corpus_count", "corpus_found",
            "bitmap_cvg", "saved_crashes", "saved_hangs", "last_crash",
            "last_hang", "stability", "slowest_exec_ms", "exec_timeout"]
    for k in keys:
        if k in stats:
            print(f"   {k:18}: {stats[k]}")

    crashes = list_inputs(os.path.join(base, "crashes"))
    hangs = list_inputs(os.path.join(base, "hangs"))
    print(f"\n   crashes on disk    : {len(crashes)}")
    print(f"   hangs on disk      : {len(hangs)}")

    # queue diversity: how many distinct original seeds + how many new finds
    qdir = os.path.join(base, "queue")
    q = [f for f in os.listdir(qdir)] if os.path.isdir(qdir) else []
    q = [f for f in q if f.startswith("id:")]
    origs = set()
    finds = 0
    for name in q:
        m = re.search(r"orig:([^,]+)", name)
        if m:
            origs.add(m.group(1))
        if "src:" in name:
            finds += 1
    print(f"\n   queue entries      : {len(q)}")
    print(f"   distinct seeds used: {len(origs)}  -> {', '.join(sorted(origs)) if origs else '(none tagged)'}")
    print(f"   new finds (src:)   : {finds}")

    def show(kind, items):
        if not items:
            return
        print(f"\n== {kind} ({len(items)}) ==")
        for p in items:
            apk = resolve_pathfile(p)
            if apk:
                exists = os.path.isfile(apk)
                print(f"   {os.path.basename(p)}")
                print(f"      -> mutant APK: {apk}  ({'present' if exists else 'MISSING (cleaned up)'})")
            else:
                print(f"   {os.path.basename(p)}  (size={os.path.getsize(p)})")

    show("CRASHES", crashes)
    show("HANGS", hangs)

    # coverage progression from plot_data — parse the header, don't assume column order
    pd = os.path.join(base, "plot_data")
    if os.path.isfile(pd):
        raw = open(pd).read().splitlines()
        header = None
        data = []
        for l in raw:
            if not l.strip():
                continue
            if l.startswith("#"):
                header = [c.strip() for c in l.lstrip("#").split(",")]
            else:
                data.append([c.strip() for c in l.split(",")])
        if data:
            def col(cols, names, default_idx):
                if header:
                    for n in names:
                        if n in header:
                            i = header.index(n)
                            if i < len(cols):
                                return cols[i]
                return cols[default_idx] if abs(default_idx) <= len(cols) else "?"
            def tcol(cols):
                return col(cols, ["relative_time"], 0)
            # prefer edges_found; fall back to map_size / total_edges across AFL versions
            def ecol(cols):
                return col(cols, ["edges_found", "total_edges"], -1) if header else cols[-1]
            def mcol(cols):
                return col(cols, ["map_size"], None) if header and "map_size" in (header or []) else None
            print(f"\n== coverage progression (plot_data) ==")
            if not header:
                print("   (no header row; showing raw first/last columns)")
            f, ln = data[0], data[-1]
            print(f"   first: t={tcol(f)}s edges_found={ecol(f)}" + (f" map_size={mcol(f)}" if mcol(f) else ""))
            print(f"   last : t={tcol(ln)}s edges_found={ecol(ln)}" + (f" map_size={mcol(ln)}" if mcol(ln) else ""))

    if args.collect and (crashes or hangs):
        os.makedirs(args.collect, exist_ok=True)
        n = 0
        for p in crashes + hangs:
            shutil.copy2(p, os.path.join(args.collect, os.path.basename(p)))
            n += 1
            apk = resolve_pathfile(p)
            if apk and os.path.isfile(apk):
                shutil.copy2(apk, os.path.join(args.collect, os.path.basename(apk)))
        print(f"\n>> collected {n} crash/hang input(s) (+ resolvable mutants) into {args.collect}")

    # nonzero exit if anything interesting was found
    return 3 if (crashes or hangs) else 0


if __name__ == "__main__":
    sys.exit(main())
