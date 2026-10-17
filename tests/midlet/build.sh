#!/bin/sh
# Builds tests/midlet/test-midlet.jar (into build/tests) against FreeJ2ME's
# own MIDP classes, with a JDK 21 javac (BOOT_JDK, else the PATH's).
# SPDX-License-Identifier: MIT
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="$root/build/tests/midlet"
javac="${BOOT_JDK:+$BOOT_JDK/bin/}javac"
jar="$root/build/jdk/images/jdk/bin/jar"
rm -rf "$out"
mkdir -p "$out/classes"
"$javac" --release 8 -nowarn -cp "$root/build/java/freej2me.jar" -d "$out/classes" "$here/src/TestMIDlet.java" 2>&1 | grep -v '^warning: \[options\]\|^1 warning$' || true
[ -f "$out/classes/TestMIDlet.class" ] || { echo "the test MIDlet did not compile" >&2; exit 1; }
"$jar" --create --date=2000-01-01T00:00:02Z --file "$out/test-midlet.jar" --manifest "$here/MANIFEST.MF" -C "$out/classes" .
echo "built $out/test-midlet.jar"
