#!/usr/bin/env python3

import sys
import zipfile
from pathlib import Path

try:
    import afl
except ModuleNotFoundError:
    class afl:
        @staticmethod
        def init():
            return None


def read_apk_path(pathfile):
    try:
        raw = Path(pathfile).read_bytes()
    except OSError:
        return None

    text = raw.replace(b"\x00", b"").decode("utf-8", errors="ignore").strip()
    if not text:
        return None

    apk_path = Path(text).expanduser()
    if not apk_path.is_file() or apk_path.suffix.lower() != ".apk":
        return None

    return apk_path


def touch_optional_entry(apk, name):
    if name not in apk.namelist():
        return

    with apk.open(name, "r") as entry:
        entry.read(4)


def test_pathfile(pathfile):
    apk_path = read_apk_path(pathfile)
    if apk_path is None:
        return

    try:
        with zipfile.ZipFile(apk_path, "r") as apk:
            names = apk.namelist()
            if "AndroidManifest.xml" not in names:
                return

            touch_optional_entry(apk, "AndroidManifest.xml")
            touch_optional_entry(apk, "classes.dex")
            touch_optional_entry(apk, "resources.arsc")
    except (OSError, RuntimeError, zipfile.BadZipFile, zipfile.LargeZipFile):
        return


def main():
    if len(sys.argv) != 2:
        print("Usage: apk_pathfile_harness.py <path_file>")
        sys.exit(1)

    afl.init()
    test_pathfile(sys.argv[1])


if __name__ == "__main__":
    main()
