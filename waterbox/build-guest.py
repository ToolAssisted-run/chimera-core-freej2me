#!/usr/bin/env python3
"""Compiles HotSpot (Zero) and the JDK's native libraries for the miniBox
guest, from the native build's compile_commands.json (build-jdk.sh): the
same sources, the same generated headers, the same defines, with the guest
toolchain's flags in place of the host's.

  build-guest.py --jdk <jdk build> --out <object dir> --sysroot <guest sysroot>
                 --ffi <libffi include dir> --kernel <kernel header dir> [-j N]

What changes for the guest, and why:
  -DCHIMERA_CORE           the patches' changes (patches/jdk21u)
  -DSTATIC_BUILD           every library linked into one program: each one's
                           JNI_OnLoad_<name> says it is there
  -DUSE_LIBRARY_BASED_TLS_ONLY, -D__thread= and the like
                           the guest has no thread-local storage of the
                           compiler's kind; HotSpot keeps its own with
                           pthread keys
  -DMUSL_LIBC              the guest's C library is musl
  -mcmodel=large ...       what every guest object is compiled with
SPDX-License-Identifier: MIT"""
import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ap = argparse.ArgumentParser()
ap.add_argument("--jdk", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--sysroot", required=True)
ap.add_argument("--ffi", required=True)
ap.add_argument("--kernel", required=True)
ap.add_argument("--zlib", required=True)
ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
a = ap.parse_args()

GCCVER = subprocess.check_output(["gcc", "-dumpfullversion"], text=True).strip()

# (module, library): java.base for the VM, java.desktop for drawing without a
# display (headless), fonts and images, java.prefs because FreeJ2ME asks
LIBS = [("java.base", l) for l in ("libjava", "libjimage", "libnet", "libnio", "libverify", "libzip")] + \
       [("java.desktop", l) for l in ("libawt", "libawt_headless", "libfontmanager", "libfreetype",
                                       "libjavajpeg", "liblcms", "libmlib_image")] + \
       [("java.prefs", "libprefs")]

GUEST = [
    "-specs=" + a.sysroot + "/lib/musl-gcc.specs",
    "-mcmodel=large", "-mno-red-zone", "-fno-pic", "-fno-pie",
    "-mstack-protector-guard=global", "-fno-stack-protector", "-fcf-protection=none",
    "-DMUSL_LIBC", "-DLIBC=musl", "-DCHIMERA_CORE", "-DSTATIC_BUILD", "-DUSE_LIBRARY_BASED_TLS_ONLY",
    "-D__thread=", "-Dthread_local=", "-D_Thread_local=",
    "-I" + a.ffi, "-g1",
    "-idirafter", a.kernel,
]
CXX_EXTRA = ["-nostdinc++", "-I%s/include/c++/%s" % (a.sysroot, GCCVER),
             "-I%s/include/c++/%s/x86_64-linux-musl" % (a.sysroot, GCCVER)]
DROP = re.compile(r"^(-fPIC|-fstack-protector.*|-D_FORTIFY_SOURCE.*|-DLIBC=gnu|-g|-gdwarf-\d|-pipe|"
                  r"-fdebug-prefix-map=.*|-frandom-seed=.*)$")


def which_lib(cmd):
    if "/hotspot/variant-zero/libjvm/objs/" in cmd:
        return "libjvm"
    for mod, lib in LIBS:
        if "/support/native/%s/%s/" % (mod, lib) in cmd:
            return lib
    return None


def guest_command(entry, lib, obj=None):
    args = shlex.split(entry["command"])
    cxx = args[0].endswith("g++")
    res = ["g++" if cxx else "gcc"]
    src = entry["file"]
    i = 1
    while i < len(args):
        x = args[i]
        if x == "-o":
            i += 2
            continue
        if x == "-c" or x == src or x.endswith("/objs/precompiled") or DROP.match(x):
            i += 1
            continue
        res.append(x)
        i += 1
    res += GUEST
    if cxx:
        res += CXX_EXTRA
    if lib != "libjvm":
        res.append("-DLIBRARY_NAME=" + lib[3:])
    if lib == "libzip":
        res.append("-I" + a.zlib)
    if obj is None:
        obj = os.path.join(a.out, lib, os.path.splitext(os.path.basename(src))[0] + ".o")
    res += ["-c", src, "-o", obj]
    return obj, res


def main():
    cc = json.load(open(os.path.join(a.jdk, "compile_commands.json")))
    work = []
    for e in cc:
        lib = which_lib(e["command"])
        if lib and "/precompiled" not in e["file"]:
            work.append(guest_command(e, lib))
    # OpenJDK's own zlib, compiled the way libzip's files are (the native
    # build uses the system's)
    template = next(e for e in cc if e["file"].endswith("/libzip/Adler32.c"))
    for f in sorted(os.listdir(a.zlib)):
        if f.endswith(".c"):
            path = os.path.realpath(os.path.join(a.zlib, f))
            e = {"file": path, "command": template["command"].replace(template["file"], path)}
            work.append(guest_command(e, "libzip", os.path.join(a.out, "libzip", "zlib-" + f[:-2] + ".o")))
    # heap.cpp, ours: compiled the way HotSpot's own files are, into libjvm
    template = next(e for e in cc if e["file"].endswith("/memory/universe.cpp"))
    heap = os.path.join(os.path.dirname(os.path.abspath(__file__)), "heap.cpp")
    e = {"file": heap, "command": template["command"].replace(template["file"], heap)}
    work.append(guest_command(e, "libjvm", os.path.join(a.out, "libjvm", "chimera_heap.o")))
    for lib in ["libjvm"] + [l for _, l in LIBS]:
        os.makedirs(os.path.join(a.out, lib), exist_ok=True)
    objs = [o for o, _ in work]
    assert len(objs) == len(set(objs)), "two sources with one object name"

    def run(item):
        obj, cmd = item
        if os.path.exists(obj) and os.path.getmtime(obj) > os.path.getmtime(cmd[-3]):
            return obj, 0, ""
        p = subprocess.run(cmd, capture_output=True, text=True)
        return obj, p.returncode, p.stderr

    failed = 0
    with ThreadPoolExecutor(a.j) as ex:
        for obj, rc, err in ex.map(run, work):
            if rc:
                failed += 1
                sys.stderr.write("FAILED %s\n%s\n" % (obj, err[-3000:]))
    print("%d objects, %d failed" % (len(work), failed))
    return 1 if failed else 0


sys.exit(main())
