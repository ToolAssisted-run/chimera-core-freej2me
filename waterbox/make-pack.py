#!/usr/bin/env python3
"""Packs files for the core's in-memory file system (memfs.c).

  make-pack.py <out> <host path>=<guest path> [...]

A host folder is packed whole, under the guest path; a file goes to the guest
path itself. Entries are sorted by guest path, so the same files make the same
pack every time.

Format: b"CHIMPAK2", u32 count, then per file u16 name length, name (UTF-8,
absolute), u64 offset, u64 size; then the files' bytes, each starting at an
offset from the pack's start that is a multiple of 4096, so that a read-only
mapping of a file can be the pack's own bytes. Little-endian.
SPDX-License-Identifier: MIT"""
import os
import struct
import sys

out = sys.argv[1]
entries = {}
for arg in sys.argv[2:]:
    host, guest = arg.split("=", 1)
    if os.path.isdir(host):
        for root, dirs, files in os.walk(host):
            dirs.sort()
            for f in sorted(files):
                hp = os.path.join(root, f)
                rel = os.path.relpath(hp, host).replace(os.sep, "/")
                entries[guest.rstrip("/") + "/" + rel] = hp
    else:
        entries[guest] = host
names = sorted(entries)
table = sum(2 + len(n.encode("utf-8")) + 16 for n in names)
offset = (12 + table + 4095) // 4096 * 4096
layout = []
for n in names:
    size = os.path.getsize(entries[n])
    layout.append((n, offset, size))
    offset = (offset + size + 4095) // 4096 * 4096
with open(out, "wb") as o:
    o.write(b"CHIMPAK2" + struct.pack("<I", len(names)))
    for n, off, size in layout:
        name = n.encode("utf-8")
        o.write(struct.pack("<H", len(name)) + name + struct.pack("<QQ", off, size))
    for n, off, size in layout:
        o.write(b"\0" * (off - o.tell()))
        with open(entries[n], "rb") as f:
            o.write(f.read())
print("%s: %d files, %d bytes" % (out, len(names), os.path.getsize(out)))
