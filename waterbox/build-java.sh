#!/bin/sh
# Builds the Java half of the core, and the runtime it runs on:
#
#   build/java/freej2me.jar   FreeJ2ME-Plus (extern/freej2me-plus), as its
#                             libretro jar is built, without the libretro and
#                             desktop front ends
#   build/java/frontend.jar   our front end and sound mixer (java/)
#   build/rt                  the Java runtime: OpenJDK's modules the game
#                             needs (jlink, from build/jdk), DejaVu fonts and
#                             their table, and the MIDI synthesizer's sound bank
#
# The native runner runs on build/rt as it is; build-guest.sh packs the same
# files into the core. Needs build-jdk.sh's build, and a JDK 21 javac (the
# boot JDK: BOOT_JDK, else javac on the PATH). FONT_DIR is where the DejaVu
# fonts are (default /usr/share/fonts/truetype/dejavu, Debian's
# fonts-dejavu-core and fonts-dejavu-extra); they must be the files
# fonts.sha256 names, as the picture depends on them. JP_FONT_DIR is where
# IPAex Gothic is, the Japanese font (default
# /usr/share/fonts/opentype/ipaexfont-gothic, Debian's
# fonts-ipaexfont-gothic), checked against fonts-ja.sha256. CN_FONT_DIR is
# where Droid Sans Fallback is, the Chinese font (default
# /usr/share/fonts/truetype/droid, Debian's fonts-droid-fallback), checked
# against fonts-zh.sha256.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
jdk="$root/build/jdk/images/jdk"
out="$root/build/java"
rt="$root/build/rt"
fonts="${FONT_DIR:-/usr/share/fonts/truetype/dejavu}"
jpfonts="${JP_FONT_DIR:-/usr/share/fonts/opentype/ipaexfont-gothic}"
cnfonts="${CN_FONT_DIR:-/usr/share/fonts/truetype/droid}"
javac="${BOOT_JDK:+$BOOT_JDK/bin/}javac"
[ -x "$jdk/bin/jlink" ] || { echo "build/jdk is not built: run waterbox/build-jdk.sh" >&2; exit 1; }
# every jar the same from the same sources
date=2000-01-01T00:00:02Z

rm -rf "$out"
mkdir -p "$out/fj" "$out/fe"

# FreeJ2ME-Plus: its sources but the libretro core (C) and the Windows gamepad
fj="$root/extern/freej2me-plus"
(cd "$fj/src" && find . -name '*.java' ! -path './libretro/*' ! -path './win32pad/*' | sort) > "$out/fj-sources"
(cd "$fj/src" && "$javac" --release 8 -nowarn -encoding utf-8 -d "$out/fj" @"$out/fj-sources" 2>&1 | grep -v '^Note: \|^warning: \[options\]\|^1 warning$' || true)
[ -f "$out/fj/org/recompile/mobile/Mobile.class" ] || { echo "FreeJ2ME-Plus did not compile" >&2; exit 1; }
# what its libretro jar leaves out, and its libretro front end, which ours replaces
(cd "$out/fj" && find . \( -name 'FJGUI*.class' -o -name 'FreeJ2ME*.class' -o -name 'ScreenShot*.class' \
	-o -name 'Libretro*.class' -o -path './org/recompile/freej2me/gamepad/*' \) -delete)
cp -R "$fj/resources/." "$out/fj/"
mkdir -p "$out/fj/META-INF"
cp -R "$fj/META-INF/." "$out/fj/META-INF/"
"$jdk/bin/jar" --create --date="$date" --file "$out/freej2me.jar" -C "$out/fj" .

# our front end, against FreeJ2ME's classes
(cd "$root/java" && find . -name '*.java' | sort) > "$out/fe-sources"
(cd "$root/java" && "$javac" --release 8 -nowarn -encoding utf-8 -cp "$out/fj" -d "$out/fe" @"$out/fe-sources" 2>&1 | grep -v '^warning: \[options\]\|^1 warning$' || true)
[ -f "$out/fe/chimera/Frontend.class" ] || { echo "the front end did not compile" >&2; exit 1; }
cp -R "$root/java/META-INF" "$out/fe/"
"$jdk/bin/jar" --create --date="$date" --file "$out/frontend.jar" -C "$out/fe" .

# the runtime: java.desktop draws without a display (headless) and plays
# sound, java.prefs is asked for by FreeJ2ME, jdk.charsets has the Japanese
# and Korean text encodings
rm -rf "$rt"
"$jdk/bin/jlink" --add-modules java.base,java.desktop,java.prefs,jdk.charsets \
	--output "$rt" --strip-debug --no-header-files --no-man-pages

# the fonts, checked
mkdir -p "$rt/lib/fonts"
(cd "$fonts" && sha256sum -c --quiet "$here/fonts.sha256") || {
	echo "the DejaVu fonts in $fonts are not the ones fonts.sha256 names (set FONT_DIR)" >&2
	exit 1
}
awk '{print $2}' "$here/fonts.sha256" | while read -r f; do cp "$fonts/$f" "$rt/lib/fonts/$f"; done
(cd "$jpfonts" && sha256sum -c --quiet "$here/fonts-ja.sha256") || {
	echo "IPAex Gothic in $jpfonts is not the file fonts-ja.sha256 names (set JP_FONT_DIR)" >&2
	exit 1
}
cp "$jpfonts/ipaexg.ttf" "$rt/lib/fonts/ipaexg.ttf"
(cd "$cnfonts" && sha256sum -c --quiet "$here/fonts-zh.sha256") || {
	echo "Droid Sans Fallback in $cnfonts is not the file fonts-zh.sha256 names (set CN_FONT_DIR)" >&2
	exit 1
}
cp "$cnfonts/DroidSansFallbackFull.ttf" "$rt/lib/fonts/DroidSansFallbackFull.ttf"
python3 "$here/make-fontconfig.py" "$rt/lib/fontconfig.properties"

# Gervill, the JDK's synthesizer, makes a sound bank the first time it is
# opened and none is installed: 50 seconds in an interpreter. It is made
# once here, by the same VM, and installed where Gervill looks first. It is
# kept in build/soundbank and made again only when its maker changes.
sb="$root/build/soundbank"
if [ ! -f "$sb/soundbank-emg.sf2" ] || [ "$here/MakeSoundbank.java" -nt "$sb/soundbank-emg.sf2" ]; then
	rm -rf "$sb"
	mkdir -p "$sb/classes"
	"$javac" --add-exports java.desktop/com.sun.media.sound=ALL-UNNAMED -d "$sb/classes" "$here/MakeSoundbank.java"
	"$rt/bin/java" --add-exports java.desktop/com.sun.media.sound=ALL-UNNAMED --add-opens java.base/java.lang=ALL-UNNAMED \
		-Duser.home="$sb" -cp "$sb/classes" MakeSoundbank "$sb/soundbank-emg.sf2.new"
	mv "$sb/soundbank-emg.sf2.new" "$sb/soundbank-emg.sf2"
fi
mkdir -p "$rt/lib/audio"
cp "$sb/soundbank-emg.sf2" "$rt/lib/audio/"
echo "built $out/freej2me.jar, $out/frontend.jar and $rt"
