#!/bin/sh
# Builds OpenJDK's Zero VM (the interpreter-only HotSpot) for this machine,
# headless, with upstream's own build: the native reference, and the source
# of build/jdk/compile_commands.json, from which build-guest.py compiles the
# same files for the sandbox.
#
#   build-jdk.sh [-j JOBS]
#
# Needs a JDK 21 to build with (the boot JDK: BOOT_JDK, else the one
# configure finds), zip, and the headers of CUPS, fontconfig and X11, which
# OpenJDK's configure asks for even for a build without a display. Where the
# packages are not installed, JDK_EXTRA_ROOT names a folder that has them
# unpacked (dpkg-deb -x): headers under usr/include, zip under usr/bin.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
jobs="$(nproc)"
while getopts j: o; do
	case "$o" in
	j) jobs="$OPTARG" ;;
	*) echo "usage: build-jdk.sh [-j JOBS]" >&2; exit 2 ;;
	esac
done

sh "$here/apply-patches.sh"
out="$root/build/jdk"
mkdir -p "$out"

if [ ! -f "$out/spec.gmk" ]; then
	set -- \
		--with-jvm-variants=zero \
		--with-jvm-features=-g1gc,-parallelgc,-shenandoahgc,-cds,-jvmti,-management,-services,-vm-structs,-jni-check \
		--enable-headless-only \
		--disable-warnings-as-errors \
		--with-debug-level=release \
		--with-native-debug-symbols=internal \
		--with-freetype=bundled \
		--with-version-opt=chimera \
		--disable-ccache
	# on WSL configure takes the machine for Windows unless it is told
	set -- "$@" --build=x86_64-pc-linux-gnu --host=x86_64-pc-linux-gnu --target=x86_64-pc-linux-gnu
	[ -n "${BOOT_JDK:-}" ] && set -- "$@" --with-boot-jdk="$BOOT_JDK"
	if [ -n "${JDK_EXTRA_ROOT:-}" ]; then
		PATH="$JDK_EXTRA_ROOT/usr/bin:$PATH"
		export PATH
		inc="$JDK_EXTRA_ROOT/usr/include"
		set -- "$@" --with-cups-include="$inc" --with-fontconfig-include="$inc" \
			--with-x=yes --x-includes="$inc" --x-libraries=/usr/lib/x86_64-linux-gnu
	fi
	(cd "$out" && bash "$root/extern/jdk21u/configure" "$@")
fi
make -C "$out" JOBS="$jobs" images compile-commands
echo "built $out/images/jdk"
