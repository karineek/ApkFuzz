#!/usr/bin/env python3
"""
test-apk-tools.py  (tasks 8 + 9)

Run mutated APKs through several external ZIP/APK tools, classify the outcome,
and preserve any specimen that makes a tool crash abnormally (segfault/abort) or
hang. Tools that are not installed are skipped (reported as 'absent'), never
treated as failures.

Tools probed (when available):
  python-zipfile   built-in structural + CRC test
  unzip -t         archive integrity test
  zipinfo -1       central-directory listing
  7z t             7-Zip integrity test
  file             libmagic identification
  aapt dump badging / apkanalyzer   Android manifest parse (if installed)

WinRAR is Windows-only; run it manually / in a VM on the preserved specimens
(see --preserve).

Per-APK classification (worst wins):
  SEGFAULT              a tool died from SIGSEGV
  TOOL_CRASH            a tool died from another signal (SIGABRT/SIGBUS/...)
  TOOL_HANG             a tool exceeded --timeout
  BROKEN_ZIP           python cannot open it as a ZIP at all
  BAD_CRC              opens, but an entry fails CRC (testzip)
  MANIFEST_PARSE_FAIL  opens, AndroidManifest.xml present but unreadable/bad CRC
  NO_MANIFEST          opens, but no AndroidManifest.xml
  INTERESTING_BEHAVIOR tools disagree (one says OK, another says broken)
  VALID_ZIP            everything consistent and healthy

Usage:
  test-apk-tools.py [options] PATH [PATH ...]
    PATH may be an .apk file or a directory (searched recursively for *.apk).
Options:
  --timeout SEC     per-tool timeout (default 30)
  --preserve DIR    copy SEGFAULT/TOOL_CRASH/TOOL_HANG specimens here
  --report FILE     write a CSV report
  --quiet           only print the summary
"""
import argparse
import csv
import os
import shutil
import signal
import subprocess
import sys
import zipfile

TOOLS = ["unzip", "zipinfo", "7z", "7za", "file", "aapt", "apkanalyzer"]

SIGNAMES = {getattr(signal, n): n for n in dir(signal)
            if n.startswith("SIG") and not n.startswith("SIG_")
            and isinstance(getattr(signal, n), int)}


def which(tool):
    return shutil.which(tool)


def run_tool(argv, timeout):
    """Return dict(rc, signal, timed_out, out). Never raises."""
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return {"rc": None, "signal": None, "timed_out": True, "out": b""}
    except FileNotFoundError:
        return {"rc": None, "signal": None, "timed_out": False, "out": b"", "absent": True}
    sig = None
    rc = p.returncode
    if rc is not None and rc < 0:
        sig = -rc
    elif rc is not None and rc > 128:
        sig = rc - 128
    return {"rc": rc, "signal": sig, "timed_out": False, "out": p.stdout or b""}


def python_zip_probe(path):
    """Structural probe via the standard library. Returns a dict."""
    r = {"opens": False, "entries": 0, "manifest_present": False,
         "testzip_bad": None, "manifest_read": None}
    try:
        z = zipfile.ZipFile(path, "r")
    except Exception as e:
        r["error"] = f"{type(e).__name__}"
        return r
    r["opens"] = True
    try:
        names = z.namelist()
        r["entries"] = len(names)
        r["manifest_present"] = "AndroidManifest.xml" in names
        try:
            r["testzip_bad"] = z.testzip()  # first bad entry name or None
        except Exception as e:
            r["testzip_bad"] = f"<raised:{type(e).__name__}>"
        if r["manifest_present"]:
            try:
                z.read("AndroidManifest.xml")
                r["manifest_read"] = "OK"
            except Exception as e:
                r["manifest_read"] = type(e).__name__
    finally:
        z.close()
    return r


def classify(py, tool_results):
    """Return (classification, note)."""
    crash = None
    for name, res in tool_results.items():
        if res.get("timed_out"):
            return "TOOL_HANG", f"{name} timed out"
        sig = res.get("signal")
        if sig:
            signame = SIGNAMES.get(sig, f"SIG{sig}")
            if sig == getattr(signal, "SIGSEGV", 11):
                return "SEGFAULT", f"{name} {signame}"
            crash = crash or (name, signame)
    if crash:
        return "TOOL_CRASH", f"{crash[0]} {crash[1]}"

    if not py.get("opens"):
        return "BROKEN_ZIP", py.get("error", "zip open failed")

    # Cross-tool disagreement: python opened it but unzip -t hard-failed, or v.v.
    unzip = tool_results.get("unzip")
    disagree = ""
    if unzip and unzip.get("rc") not in (None, 0) and py.get("opens") \
       and py.get("testzip_bad") is None and py.get("manifest_read") == "OK":
        disagree = "python=OK but unzip=fail"

    if py.get("manifest_present"):
        mr = py.get("manifest_read")
        if mr and mr != "OK":
            return "MANIFEST_PARSE_FAIL", f"manifest_read={mr}" + (f"; {disagree}" if disagree else "")
    else:
        return "NO_MANIFEST", disagree or "no AndroidManifest.xml"

    bad = py.get("testzip_bad")
    if bad:
        return "BAD_CRC", f"first_bad={bad}"

    if disagree:
        return "INTERESTING_BEHAVIOR", disagree
    return "VALID_ZIP", ""


def collect_apks(paths):
    out = []
    for p in paths:
        if os.path.isdir(p):
            for root, _, files in os.walk(p):
                for f in files:
                    if f.lower().endswith(".apk"):
                        out.append(os.path.join(root, f))
        elif p.lower().endswith(".apk"):
            out.append(p)
        else:
            print(f"skip (not .apk / not dir): {p}", file=sys.stderr)
    return sorted(out)


def tool_cmd(tool, path):
    return {
        "unzip":      [tool, "-t", path],
        "zipinfo":    [tool, "-1", path],
        "7z":         [tool, "t", path],
        "7za":        [tool, "t", path],
        "file":       [tool, path],
        "aapt":       [tool, "dump", "badging", path],
        "apkanalyzer":[tool, "manifest", "print", path],
    }[tool]


def main():
    ap = argparse.ArgumentParser(description="Run mutated APKs through external ZIP/APK tools.")
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--timeout", type=int, default=30)
    ap.add_argument("--preserve")
    ap.add_argument("--report")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    available = [t for t in TOOLS if which(t)]
    # avoid double-running 7z and 7za
    if "7z" in available and "7za" in available:
        available.remove("7za")
    print(f">> tools available: {', '.join(available) or '(none but python-zipfile)'}", file=sys.stderr)
    absent = [t for t in ("unzip", "zipinfo", "7z", "file", "aapt") if not which(t) and (t != "7z" or not which("7za"))]
    if absent:
        print(f">> tools absent (skipped): {', '.join(absent)}", file=sys.stderr)

    apks = collect_apks(args.paths)
    if not apks:
        print("no .apk inputs found", file=sys.stderr)
        return 2

    if args.preserve:
        os.makedirs(args.preserve, exist_ok=True)

    rows = []
    counts = {}
    flagged = []
    for apk in apks:
        py = python_zip_probe(apk)
        tool_results = {}
        for t in available:
            tool_results[t] = run_tool(tool_cmd(t, apk), args.timeout)
        cls, note = classify(py, tool_results)
        counts[cls] = counts.get(cls, 0) + 1

        if cls in ("SEGFAULT", "TOOL_CRASH", "TOOL_HANG"):
            flagged.append((apk, cls, note))
            if args.preserve:
                try:
                    shutil.copy2(apk, os.path.join(args.preserve, os.path.basename(apk)))
                except Exception as e:
                    print(f"   (preserve failed for {apk}: {e})", file=sys.stderr)

        row = {
            "apk": apk, "classification": cls, "note": note,
            "opens": py.get("opens"), "entries": py.get("entries"),
            "manifest_present": py.get("manifest_present"),
            "manifest_read": py.get("manifest_read"),
            "testzip_bad": py.get("testzip_bad"),
        }
        for t in available:
            r = tool_results[t]
            if r.get("timed_out"):
                v = "HANG"
            elif r.get("signal"):
                v = SIGNAMES.get(r["signal"], f"SIG{r['signal']}")
            else:
                v = f"rc={r.get('rc')}"
            row[f"tool_{t}"] = v
        rows.append(row)
        if not args.quiet:
            marker = "  <-- FLAG" if cls in ("SEGFAULT", "TOOL_CRASH", "TOOL_HANG") else ""
            print(f"{cls:20} {os.path.basename(apk):45} {note}{marker}")

    print("\n== summary ==", file=sys.stderr)
    for k in sorted(counts):
        print(f"   {k:22} {counts[k]}", file=sys.stderr)
    if flagged:
        print("\n== FLAGGED (tool crash/hang) ==", file=sys.stderr)
        for apk, cls, note in flagged:
            print(f"   {cls:12} {apk}  ({note})", file=sys.stderr)
        if args.preserve:
            print(f"   specimens copied to: {args.preserve}", file=sys.stderr)

    if args.report:
        fields = ["apk", "classification", "note", "opens", "entries",
                  "manifest_present", "manifest_read", "testzip_bad"] + \
                 [f"tool_{t}" for t in available]
        with open(args.report, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=fields)
            w.writeheader()
            for r in rows:
                w.writerow(r)
        print(f">> report: {args.report}", file=sys.stderr)

    # exit code: 3 if any tool crash/hang (interesting!), else 0
    return 3 if flagged else 0


if __name__ == "__main__":
    sys.exit(main())
