import os, random, struct, sys, zipfile

path = sys.argv[1] if len(sys.argv) > 1 else "F-Droid1.apk"


def apk_entry_data_range(apk_path, entry_name):
    with zipfile.ZipFile(apk_path, "r") as apk:
        info = apk.getinfo(entry_name)

    with open(apk_path, "rb") as f:
        f.seek(info.header_offset)
        header = f.read(30)

    if len(header) != 30 or header[:4] != b"PK\x03\x04":
        raise SystemExit(f"Bad ZIP local header for {entry_name}")

    name_len, extra_len = struct.unpack_from("<HH", header, 26)
    start = info.header_offset + 30 + name_len + extra_len
    end = start + info.compress_size

    return start, end


try:
    i, j = apk_entry_data_range(path, "AndroidManifest.xml")
except KeyError:
    raise SystemExit("AndroidManifest.xml not found in APK")
except zipfile.BadZipFile:
    raise SystemExit("Bad ZIP/APK file")

if i < 0 or j <= i:
    raise SystemExit("Need 0 <= i < j")

size = os.path.getsize(path)
if j > size:
    raise SystemExit(f"Range end j={j} is past EOF (size={size})")

# pick a byte offset and a bit index
off = random.randrange(i, j)     # byte offset
bit = random.randrange(0, 8)     # 0..7 (LSB..MSB)
mask = 1 << bit

with open(path, "r+b") as f:
    f.seek(off)
    b = f.read(1)
    if not b:
        raise SystemExit("Unexpected EOF")
    old = b[0]
    new = old ^ mask
    f.seek(off)
    f.write(bytes([new]))

print(f"Flipped bit {bit} at byte offset {off}: {old:#04x} -> {new:#04x}")
