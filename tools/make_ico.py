#!/usr/bin/env python3
"""Pack PNG files into a Windows .ico (PNG-compressed entries, Vista+).

Usage:
    make_ico.py OUT.ico SIZE:PNG [SIZE:PNG ...]
e.g. make_ico.py icon.ico 16:16.png 32:32.png 48:48.png 256:256.png
"""
import struct
import sys


def main(argv):
    out = argv[1]
    imgs = []
    for spec in argv[2:]:
        size, path = spec.split(":", 1)
        with open(path, "rb") as f:
            imgs.append((int(size), f.read()))
    n = len(imgs)
    header = struct.pack("<HHH", 0, 1, n)
    entries = b""
    offset = 6 + 16 * n
    for size, data in imgs:
        dim = 0 if size >= 256 else size  # 0 means 256 in the ICO format
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    with open(out, "wb") as f:
        f.write(header + entries + b"".join(d for _, d in imgs))
    print(f"wrote {out} ({n} sizes)")


if __name__ == "__main__":
    main(sys.argv)
