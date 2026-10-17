#!/bin/sh
# Builds build/bin/run-native: the core's exports (core.c, driver.c)
# compiled for this machine, over the host's own Zero VM and runtime
# (build-jdk.sh and build-java.sh). It shows a game working outside the
# sandbox; see run-native.c for what it can and cannot be compared with.
#
#   build-native.sh -m <miniBox checkout>     (for the guest kit's headers)
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-}"
while getopts m: o; do
	case "$o" in
	m) mb="$OPTARG" ;;
	*) echo "usage: build-native.sh -m <miniBox>" >&2; exit 2 ;;
	esac
done
[ -n "$mb" ] || { echo "build-native.sh: say where miniBox is (-m, or MINIBOX_DIR)" >&2; exit 2; }
mb="$(cd "$mb" && pwd)"
jb="$root/build/jdk"
rt="$root/build/rt"
[ -f "$rt/lib/zero/libjvm.so" ] || { echo "build/rt is not built: run waterbox/build-java.sh" >&2; exit 1; }
mkdir -p "$root/build/bin"
gcc -O2 -Wall -g -I"$here/native-shim" -I"$mb/source/guest/include" -I"$mb/extern/jsmn" \
	-I"$jb/support/modules_include/java.base" -I"$jb/support/modules_include/java.base/linux" \
	-DCORE_HOME="\"$rt\"" -DCORE_CLASSPATH="\"$root/build/java/freej2me.jar:$root/build/java/frontend.jar\"" \
	-o "$root/build/bin/run-native" "$here/run-native.c" "$here/core.c" "$here/driver.c" \
	-L"$rt/lib/zero" -ljvm -lpthread -Wl,-rpath,"$rt/lib/zero"
echo "built $root/build/bin/run-native"
