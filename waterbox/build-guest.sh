#!/bin/sh
# Builds the core for the sandbox: build/guest/core.wbx, and run-wbx, the
# runner that drives it through miniBox's host.
#
#   build-guest.sh -m <miniBox checkout> [-j JOBS]
#
# The miniBox checkout needs its host library and its guest toolchain built
# (build/meson-linux and build/meson-cpp; Chimera's AGENTS.md says how).
# Needs build-jdk.sh's and build-java.sh's builds.
#
# Steps: the kernel's headers, which OpenJDK includes and the guest's C
# library has not; libffi, which Zero calls native code through; HotSpot and
# the JDK's native libraries (build-guest.py); the pack of the runtime's
# files (make-pack.py); then the link, with the table dlsym answers from and
# every file call routed to memfs.c.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-}"
jobs="$(nproc)"
while getopts m:j: o; do
	case "$o" in
	m) mb="$OPTARG" ;;
	j) jobs="$OPTARG" ;;
	*) echo "usage: build-guest.sh -m <miniBox> [-j JOBS]" >&2; exit 2 ;;
	esac
done
[ -n "$mb" ] || { echo "build-guest.sh: say where miniBox is (-m, or MINIBOX_DIR)" >&2; exit 2; }
mb="$(cd "$mb" && pwd)"
sr="$mb/build/meson-cpp/guest-sysroot"
mbuild="$mb/build/meson-cpp"
mbhost="$mb/build/meson-linux/source/host"
[ -f "$sr/lib/musl-gcc.specs" ] || { echo "miniBox's guest toolchain is not built ($mbuild)" >&2; exit 1; }
jb="$root/build/jdk"
[ -f "$jb/compile_commands.json" ] || { echo "build/jdk is not built: run waterbox/build-jdk.sh" >&2; exit 1; }
[ -f "$root/build/java/frontend.jar" ] || { echo "build/java is not built: run waterbox/build-java.sh" >&2; exit 1; }
g="$root/build/guest"
mkdir -p "$g"

# the kernel's headers (Debian's linux-libc-dev), and X11's, fontconfig's and
# CUPS's, which OpenJDK's headless AWT includes: the guest's include path is the
# guest C library's, not the build machine's, so they are copied where it
# looks last (from the folder build-jdk.sh took them from)
kin="${KERNEL_INCLUDE:-/usr/include}"
if [ ! -d "$g/kernel-include/linux" ]; then
	mkdir -p "$g/kernel-include"
	cp -R "$kin/linux" "$kin/asm-generic" "$g/kernel-include/"
	if [ -d "$kin/x86_64-linux-gnu/asm" ]; then cp -R "$kin/x86_64-linux-gnu/asm" "$g/kernel-include/"; else cp -R "$kin/asm" "$g/kernel-include/"; fi
fi
xin="${JDK_EXTRA_ROOT:+$JDK_EXTRA_ROOT/usr/include}"
xin="${xin:-/usr/include}"
for d in X11 fontconfig cups; do
	[ -d "$g/kernel-include/$d" ] || cp -R "$xin/$d" "$g/kernel-include/"
done

# logged <log> <command...>: runs it with its output in the log, and on a
# failure shows the log's end (a runner keeps nothing else)
logged() {
	lg_log="$1"; shift
	if ! "$@" > "$lg_log" 2>&1; then
		echo "failed: $* (the end of $lg_log:)" >&2
		tail -40 "$lg_log" >&2
		exit 1
	fi
}

gcc_guest="gcc -specs=$sr/lib/musl-gcc.specs -mcmodel=large -mno-red-zone -mstack-protector-guard=global -fno-stack-protector -fno-pic -fno-pie -fcf-protection=none"

# libffi, from its git tree (autogen.sh makes its configure)
if [ ! -f "$g/libffi/lib/libffi.a" ]; then
	rm -rf "$g/libffi-src" "$g/libffi-build"
	mkdir -p "$g/libffi-src" "$g/libffi-build"
	(cd "$root/extern/libffi" && git archive HEAD) | tar -x -C "$g/libffi-src"
	(cd "$g/libffi-src" && logged "$g/libffi-autogen.log" ./autogen.sh)
	(cd "$g/libffi-build" && logged "$g/libffi-configure.log" "$g/libffi-src/configure" --host=x86_64-linux-musl \
		--build=x86_64-pc-linux-gnu --prefix="$g/libffi" --disable-shared --enable-static --disable-exec-static-tramp \
		--disable-docs --with-pic=no CC="$gcc_guest -O2" CFLAGS=)
	logged "$g/libffi-make.log" make -C "$g/libffi-build" -j "$jobs" install
fi

# HotSpot and the native libraries
python3 "$here/build-guest.py" --jdk "$jb" --out "$g/obj" --sysroot "$sr" --ffi "$g/libffi/include" \
	--kernel "$g/kernel-include" --zlib "$root/extern/jdk21u/src/java.base/share/native/libzip/zlib" -j "$jobs"

# the pack: the runtime's files the VM reads (no programs, no native
# libraries - those are linked in), and the two jars
rt="$root/build/rt"
p="$g/pack-root"
rm -rf "$p"
mkdir -p "$p/jdk/lib" "$p/chimera"
cp -R "$rt/conf" "$p/jdk/"
cp "$rt/release" "$p/jdk/"
(cd "$rt/lib" && find . -type f ! -name '*.so' ! -path './zero/*' ! -name jexec ! -name jspawnhelper ! -name jrt-fs.jar ! -name jvm.cfg | sort) |
	while read -r f; do mkdir -p "$p/jdk/lib/$(dirname "$f")"; cp "$rt/lib/$f" "$p/jdk/lib/$f"; done
cp "$root/build/java/freej2me.jar" "$root/build/java/frontend.jar" "$p/chimera/"
python3 "$here/make-pack.py" "$g/core.pack" "$p=/"

# the table dlsym answers from: every function the JDK's native libraries define
jdklibs="libjava libjimage libnet libnio libverify libzip libawt libawt_headless libfontmanager libfreetype libjavajpeg liblcms libmlib_image libprefs"
o="$g/obj/core"
mkdir -p "$o"
# libawt_headless has its own copy of systemScale.c, which libawt has too
objs="$(for l in $jdklibs; do ls "$g/obj/$l/"*.o; done | grep -v '/libawt_headless/systemScale\.o$')"
# shellcheck disable=SC2086
nm --defined-only -g $objs | awk '$2=="T"{print $3}' | sort -u > "$o/symbols.txt"
# Java takes a library as linked in when JNI_OnLoad_<name> exists; the ones
# with no JNI_OnLoad of their own get one that only says which JNI version
# they need
missing=""
for l in $jdklibs; do
	n=${l#lib}
	grep -qx "JNI_OnLoad_$n" "$o/symbols.txt" || missing="$missing $n"
done
{
	echo '/* generated by build-guest.sh */'
	echo '#include <jni.h>'
	for n in $missing; do
		echo "JNIEXPORT jint JNICALL JNI_OnLoad_$n(JavaVM *vm, void *reserved) { (void)vm; (void)reserved; return JNI_VERSION_1_8; }"
	done
} > "$o/onload-stubs.c"
for n in $missing; do echo "JNI_OnLoad_$n"; done >> "$o/symbols.txt"
sort -u -o "$o/symbols.txt" "$o/symbols.txt"
{
	echo '/* generated by build-guest.sh */'
	echo 'struct core_symbol { const char *name; void *addr; };'
	sed 's/.*/extern void &(void);/' "$o/symbols.txt"
	echo 'const struct core_symbol core_symbols[] = {'
	sed 's/.*/\t{ "&", (void *)\&& },/' "$o/symbols.txt"
	echo '};'
	echo "const int core_symbol_count = $(wc -l < "$o/symbols.txt");"
} > "$o/core-symbols.c"

cflags="-O2 -Wall -fvisibility=hidden -DCHIMERA_GUEST -I$mb/extern/emulibc -I$mb/source/guest/include -I$mb/extern/jsmn
	-I$jb/support/modules_include/java.base -I$jb/support/modules_include/java.base/linux -I$here"
for f in core driver guest-shims memfs; do
	# shellcheck disable=SC2086
	$gcc_guest $cflags -c "$here/$f.c" -o "$o/$f.o"
done
# shellcheck disable=SC2086
$gcc_guest $cflags -w -c "$o/core-symbols.c" -o "$o/core-symbols.o"
# shellcheck disable=SC2086
$gcc_guest $cflags -c "$o/onload-stubs.c" -o "$o/onload-stubs.o"
$gcc_guest -DPACK_PATH="\"$g/core.pack\"" -c "$here/pack.S" -o "$o/pack.o"

wraps="mmap munmap sysconf open creat read readv write writev pread pwrite lseek close fstat stat lstat access fcntl
	ftruncate fsync fdatasync isatty fstatvfs statvfs mkdir unlink rmdir remove rename chmod fchmod chown utimes
	link symlink readlink chdir flock posix_fadvise opendir readdir closedir fopen"
wrapflags="$(for w in $wraps; do printf -- '-Wl,--wrap=%s ' "$w"; done)"
# shellcheck disable=SC2086
g++ -specs="$sr/lib/musl-gcc.specs" -mcmodel=large -fno-pic -fno-pie \
	-static -no-pie -Wl,--eh-frame-hdr,-O2,--no-relax,-z,stack-size=8388608 -T "$mb/source/guest/linkscript.T" \
	-Wl,-u,pthread_once -Wl,-u,pthread_cond_wait -Wl,-u,pthread_cond_broadcast -Wl,-u,pthread_key_create \
	-Wl,-u,pthread_mutexattr_init -Wl,-u,pthread_mutexattr_settype -Wl,-u,pthread_mutexattr_destroy \
	$wrapflags \
	-o "$g/core.wbx" \
	"$o"/core.o "$o"/driver.o "$o"/guest-shims.o "$o"/memfs.o "$o"/core-symbols.o "$o"/onload-stubs.o "$o"/pack.o \
	"$g"/obj/libjvm/*.o $objs \
	"$mbuild/source/guest/cxxglue.c.o" "$mbuild/source/guest/emulibc.c.o" \
	"$g/libffi/lib/libffi.a" \
	-L"$sr/lib" -lstdc++ -lgcc -lgcc_eh -lc
# no debugging information: it was a fifth of the file, and nothing in the
# sandbox reads it. The function names stay, for a crash report
# (chimera#301: core.wbx 108.8 MB to 84.8 MB, the package 40.7 MB to 32.8 MB)
strip --strip-debug "$g/core.wbx"
sh "$mb/source/guest/check-wbx.sh" "$g/core.wbx"
echo "built $g/core.wbx"

# the runner, against miniBox's host
mkdir -p "$root/build/bin"
gcc -O2 -Wall -I"$mb/source/host" -I"$here" -o "$root/build/bin/run-wbx" "$here/run-wbx.c" \
	"$mbhost/libminiboxhost.so" -Wl,-rpath,"$mbhost"
echo "built $root/build/bin/run-wbx"
