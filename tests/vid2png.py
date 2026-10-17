#!/usr/bin/env python3
"""Turns a runner's --vid-out picture ("W H\\n", then BGRA rows) into a PNG.

  vid2png.py <in> <out.png>
SPDX-License-Identifier: MIT"""
import struct
import sys
import zlib

data = open(sys.argv[1], "rb").read()
nl = data.index(b"\n")
w, h = map(int, data[:nl].split())
px = data[nl + 1:]
rows = b"".join(b"\0" + bytes(c for i in range(w) for c in (px[(y * w + i) * 4 + 2], px[(y * w + i) * 4 + 1], px[(y * w + i) * 4]))
               for y in range(h))


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)


with open(sys.argv[2], "wb") as o:
    o.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
