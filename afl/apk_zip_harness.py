#!/usr/bin/env python3

import sys
import struct
import zipfile

try:
    import afl
except ModuleNotFoundError:
    class afl:
        @staticmethod
        def init():
            return None


def entry_data_range(apk_path, entry_info):
    with open(apk_path, "rb") as f:
        f.seek(entry_info.header_offset)
        header = f.read(30)

    if len(header) != 30 or header[:4] != b"PK\x03\x04":
        raise ValueError("bad ZIP local header")

    name_len, extra_len = struct.unpack_from("<HH", header, 26)
    start = entry_info.header_offset + 30 + name_len + extra_len
    end = start + entry_info.compress_size

    return start, end


def test_apk(apk_path):
    # AFL gives us a mutated APK file.
    # APK files are ZIP-based, so we first try to open it as a ZIP.
    try:
        with zipfile.ZipFile(apk_path, "r") as apk:
            names = apk.namelist()

            # Basic APK structure checks.
            # These branches give AFL something meaningful to explore.
            if "AndroidManifest.xml" in names:
                manifest_info = apk.getinfo("AndroidManifest.xml")
                manifest_start, manifest_end = entry_data_range(apk_path, manifest_info)

                if manifest_start < manifest_end:
                    pass

                manifest_data = apk.read("AndroidManifest.xml")
                if manifest_data.startswith(b"\x03\x00\x08\x00"):
                    pass

            if "classes.dex" in names:
                pass

            if "resources.arsc" in names:
                pass

            # Walk over the entries and touch common APK file types.
            for entry in apk.infolist():
                if entry.filename.endswith(".dex"):
                    pass
                elif entry.filename.endswith(".xml"):
                    pass
                elif entry.filename.startswith("META-INF/"):
                    pass

    except zipfile.BadZipFile:
        # Bad ZIP/APK files are expected during fuzzing.
        # We ignore them because they are not interesting Python crashes.
        return

    except Exception:
        # Any unexpected exception should be visible to AFL.
        raise


def main():
    if len(sys.argv) != 2:
        print("Usage: apk_zip_harness.py <apk_file>")
        sys.exit(1)

    # Initialize python-afl after imports/setup and before testing the input.
    afl.init()

    test_apk(sys.argv[1])


if __name__ == "__main__":
    main()
