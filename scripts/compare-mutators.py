#!/usr/bin/env python3
"""
compare-mutators.py  (task 13)

Aggregate several AFL++ runs (typically one per mutator strategy) into a single
comparison table, so you can decide which mutator is best for which phase.

Each positional argument is either:
  LABEL=OUTDIR    e.g.  segment=out-manifest-segment
  OUTDIR          label is inferred from the directory name

For every run it reads fuzzer_stats (OUTDIR/default/fuzzer_stats if present) and,
if a tool-tester CSV is given via --tools LABEL=report.csv, folds in the
broken/crash rates from test-apk-tools.py.

Metrics compared:
  execs_done, execs_per_sec, corpus_count, corpus_found (new coverage inputs),
  bitmap_cvg, saved_crashes, saved_hangs, run_time
  + (optional) tool_specimens, tool_valid_zip, tool_bad_crc, tool_broken_zip,
    tool_crash_or_segfault

Usage:
  compare-mutators.py LABEL=OUTDIR [LABEL=OUTDIR ...] [--tools LABEL=report.csv ...]
                      [--csv out.csv]
"""
import argparse
import csv
import os
import sys


def read_stats(outdir):
    base = outdir
    if os.path.isdir(os.path.join(base, "default")):
        base = os.path.join(base, "default")
    path = os.path.join(base, "fuzzer_stats")
    stats = {}
    if os.path.isfile(path):
        for line in open(path, errors="ignore"):
            if ":" in line:
                k, _, v = line.partition(":")
                stats[k.strip()] = v.strip()
    return stats


def read_tool_report(path):
    """Summarize a test-apk-tools.py CSV into class counts."""
    counts = {}
    if not os.path.isfile(path):
        return counts
    with open(path) as f:
        for row in csv.DictReader(f):
            c = row.get("classification", "?")
            counts[c] = counts.get(c, 0) + 1
    return counts


def parse_label(arg):
    if "=" in arg:
        label, _, path = arg.partition("=")
        return label, path
    return os.path.basename(os.path.normpath(arg)), arg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("runs", nargs="+", help="LABEL=OUTDIR or OUTDIR")
    ap.add_argument("--tools", action="append", default=[], help="LABEL=report.csv")
    ap.add_argument("--csv")
    args = ap.parse_args()

    tool_map = dict(parse_label(t) for t in args.tools)

    def g(s, k):
        try:
            return s.get(k, "")
        except Exception:
            return ""

    cols = ["label", "run_time", "execs_done", "execs_per_sec", "corpus_count",
            "corpus_found", "bitmap_cvg", "saved_crashes", "saved_hangs"]
    tool_cols = ["tool_total", "tool_VALID_ZIP", "tool_BAD_CRC", "tool_BROKEN_ZIP",
                 "tool_MANIFEST_PARSE_FAIL", "tool_SEGFAULT", "tool_TOOL_CRASH", "tool_TOOL_HANG"]

    rows = []
    have_tools = bool(tool_map)
    seen_labels = set()
    for arg in args.runs:
        label, outdir = parse_label(arg)
        seen_labels.add(label)
        s = read_stats(outdir)
        if not s:
            print(f"WARNING: no fuzzer_stats for run '{label}' "
                  f"(looked in {outdir}[/default]/fuzzer_stats) — row will be blank",
                  file=sys.stderr)
        row = {"label": label}
        for k in cols[1:]:
            row[k] = g(s, k)
        if have_tools and label in tool_map:
            tc = read_tool_report(tool_map[label])
            if not tc:
                print(f"WARNING: tool report for '{label}' is missing or empty "
                      f"({tool_map[label]})", file=sys.stderr)
            row["tool_total"] = sum(tc.values())
            for c in ["VALID_ZIP", "BAD_CRC", "BROKEN_ZIP", "MANIFEST_PARSE_FAIL",
                      "SEGFAULT", "TOOL_CRASH", "TOOL_HANG"]:
                row[f"tool_{c}"] = tc.get(c, 0)
        rows.append(row)

    # warn about --tools labels that don't match any run
    for lbl in tool_map:
        if lbl not in seen_labels:
            print(f"WARNING: --tools label '{lbl}' does not match any run label",
                  file=sys.stderr)

    header = cols + (tool_cols if have_tools else [])
    widths = {h: max(len(h), *(len(str(r.get(h, ""))) for r in rows)) for h in header}
    print("  ".join(h.ljust(widths[h]) for h in header))
    print("  ".join("-" * widths[h] for h in header))
    for r in rows:
        print("  ".join(str(r.get(h, "")).ljust(widths[h]) for h in header))

    if args.csv:
        with open(args.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=header)
            w.writeheader()
            for r in rows:
                w.writerow({h: r.get(h, "") for h in header})
        print(f"\n>> wrote {args.csv}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
