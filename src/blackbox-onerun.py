import os, random, sys

path = "F-Droid1.apk"
i = 7401341
j = i + 8921

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
