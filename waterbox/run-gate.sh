#!/bin/bash
# The core's tests. Each line says what was compared. The script ends with a
# count of tests that passed, failed and were skipped, and with an error if
# any failed. A skipped test is not a pass.
#
#   waterbox/run-gate.sh
#
# CHIMERA_ROOT=<a Chimera checkout> adds the tests that need Chimera: the
# key names its bindings may use, and the package played in Chimera's own
# headless runner (waterbox/build-package.sh -r <it> first).
#
# Needs the builds docs/BUILDING.md lists: build/guest/core.wbx and
# build/bin/run-wbx (build-guest.sh), build/bin/run-native
# (build-native.sh), and the test MIDlet (tests/midlet/build.sh).
#
# WHAT RUNS WITH NO FILES OF YOURS: everything but the game: tests. The test
# MIDlet (tests/midlet) is this repository's own: a square moved by the
# keys, a timer, text, a record store, a touch mark, a MIDI tune, a tone and
# a beep. The game: tests run five commercial games where tests/games-local
# (or GAMES_DIR) has them, named and checked by SHA-1, and SKIP elsewhere.
#
# Written against the ways a test passes on a broken thing (Chimera's
# docs/gates.md): a machine that did nothing agrees with itself, so a run is
# first checked to have got somewhere; the end of a run hides a run that went
# another way and came back, so runs are compared as a stream of digests;
# and a comparison that cannot fail proves nothing, so each "the same" has a
# run beside it that must come out different.
#
# What is NOT compared: the native runner against the sandbox. A Java
# program's threads run on the host's clock natively and on the machine's
# clock in the sandbox, so the two draw different frames; the native runner
# is only checked to run.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
native="$root/build/bin/run-native"
runwbx="$root/build/bin/run-wbx"
core="$root/build/guest/core.wbx"
midlet="$root/build/tests/midlet/test-midlet.jar"
chimera_root="${CHIMERA_ROOT:-}"

work="$(mktemp -d)"
# GATE_KEEP=1 keeps the work folder (the runs' output) and says where
if [ -n "${GATE_KEEP:-}" ]; then trap 'echo "work folder kept: $work"' EXIT; else trap 'rm -rf "$work"' EXIT; fi
ok=0; failed=0; skipped=0
report() { # <test> <PASS|FAIL|SKIP> <what>
	printf '%-22s %-6s %s\n' "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok + 1)) ;; FAIL) failed=$((failed + 1)) ;; *) skipped=$((skipped + 1)) ;; esac
}
# cond '<shell condition>': a condition of several parts, run in this shell
# (where the functions below are)
cond() { eval "$1"; }
check() { # <test> <what> <condition...>
	ck_test="$1"; ck_what="$2"; shift 2
	if "$@"; then report "$ck_test" PASS "$ck_what"; else report "$ck_test" FAIL "$ck_what"; fi
}
stream() { grep '^frame' "$1"; }
# two runs' digests (the report lines and the state at the end) are the same
same() { [ -s "$1" ] && [ -s "$2" ] && [ "$(grep '^frame\|^state' "$1")" = "$(grep '^frame\|^state' "$2")" ]; }
# the report lines of two runs: the same up to frame <n> and different after
same_until() { # <a> <b> <n>
	[ "$(stream "$1" | awk -v n="$3" '$2 <= n' | md5sum)" = "$(stream "$2" | awk -v n="$3" '$2 <= n' | md5sum)" ] &&
	[ "$(stream "$1" | awk -v n="$3" '$2 > n' | md5sum)" != "$(stream "$2" | awk -v n="$3" '$2 > n' | md5sum)" ]
}
pictures() { stream "$1" | awk '{print $7}' | sort -u | wc -l; }
sounds() { stream "$1" | awk '{print $10}' | sort -u | wc -l; }
lines() { stream "$1" | wc -l; }
# folder <name> <settings json> [<game file>]: a work folder as the sandbox
# mounts a project - every file under its name
folder() {
	mkdir -p "$work/$1"
	if [ -n "${3:-}" ]; then
		cp "$3" "$work/$1/$(basename "$3")"
		printf '{"game":["%s"]}' "$(basename "$3")" > "$work/$1/slots"
	else
		printf '{"game":[]}' > "$work/$1/slots"
	fi
	printf '%s' "$2" > "$work/$1/settings"
}
nat() { nt_out="$1"; nt_dir="$2"; shift 2; "$native" "$work/$nt_dir" "$@" > "$work/$nt_out.out" 2> "$work/$nt_out.err"; }
box() { bx_out="$1"; bx_dir="$2"; shift 2; "$runwbx" "$core" "$work/$bx_dir" "$@" > "$work/$bx_out.out" 2> "$work/$bx_out.err"; }
index() { "$runwbx" "$core" "$work/$1" --frames 1 --list-panel 2> /dev/null | grep "'$2'" | head -1 | awk '{print $2}'; }
# repeats <file> <first> <second> <count> <step>: do the report lines from
# frame <second> on repeat those from frame <first> on (run-wbx --rewind)
repeats() {
	awk -v a="$2" -v b="$3" -v n="$4" -v step="$5" '
		/^frame/ { f = $2; $1 = ""; $2 = ""; sub(/ lag [0-9]+$/, ""); line[f] = $0 }
		END {
			for (i = step; i < n; i += step) {
				if (!((a + i) in line) || !((b + i) in line) || line[a + i] != line[b + i]) exit 1
				seen++
			}
			exit seen > 0 ? 0 : 1
		}' "$1"
}

if [ ! -x "$runwbx" ] || [ ! -f "$core" ] || [ ! -f "$midlet" ]; then
	echo "not built: this needs build/guest/core.wbx, build/bin/run-wbx and build/tests/midlet/test-midlet.jar (docs/BUILDING.md)" >&2
	exit 1
fi

# ------------------------------------------------- the source and the package
pins=1
for sub in jdk21u freej2me-plus libffi; do
	[ "$(git -C "$root" rev-parse "HEAD:extern/$sub" 2>/dev/null)" = "$(git -C "$root/extern/$sub" rev-parse HEAD 2>/dev/null)" ] || pins=0
done
patches="$(ls "$root"/patches/*/*.patch 2> /dev/null | wc -l)"
check "source:patched" "the three submodules are at their pinned commits, with the $patches patches of patches/" \
	sh -c "sh '$here/apply-patches.sh' > '$work/patches.log' 2>&1 && [ $pins = 1 ]"

stale=""
for src in "$here"/core.c "$here"/driver.c "$here"/driver.h "$here"/guest-shims.c "$here"/memfs.c "$here"/memfs.h "$here"/heap.cpp "$here"/pack.S \
	"$root"/patches/*/*.patch $(find "$root/java" -name '*.java'); do
	[ -e "$src" ] || continue
	[ "$core" -nt "$src" ] || stale="$stale $(basename "$src")"
done
check "package:fresh" "core.wbx is not older than its sources${stale:+ (older than:$stale)}" [ -z "$stale" ]

check "package:no-debug-info" "core.wbx carries no debugging information, which nothing in the sandbox reads ($(stat -c %s "$core") bytes)" \
	sh -c "! readelf -S '$core' | grep -q '\.debug_'"

folder t '{}' "$midlet"
"$runwbx" "$core" "$work/t" --frames 1 --list-panel > "$work/panel.out" 2> /dev/null
check "package:controls" "the package declares the core's own $(grep -c '^panel' "$work/panel.out") buttons, in the core's order, and its 2 axes" \
	python3 - "$here/waterbox.config" "$work/panel.out" <<'PY'
import json, sys
cfg = json.load(open(sys.argv[1]))
lines = open(sys.argv[2]).read().splitlines()
buttons = [l[l.index("'") + 1:l.rindex("'")] for l in lines if l.startswith("panel ")]
axes = [l for l in lines if l.startswith("axis ")]
i = cfg["input"]
sys.exit(0 if i["buttons"] == buttons and len(buttons) == 21 and [a["name"] for a in i["axes"]] == ["Touch X", "Touch Y"] and len(axes) == 2 else 1)
PY

if [ -n "$chimera_root" ] && [ -f "$chimera_root/source/ui/qt/key_codes.inc" ]; then
	check "package:key-names" "every host key the package's default bindings name is a key Chimera knows: $(python3 "$root/tests/check-key-names.py" "$chimera_root" "$here/default_keybinds.json" 2>&1)" \
		sh -c "python3 '$root/tests/check-key-names.py' '$chimera_root' '$here/default_keybinds.json' > /dev/null 2>&1"
else
	report "package:key-names" SKIP "needs CHIMERA_ROOT, whose source names the keys Chimera knows"
fi

# ------------------------------------------------- the machine runs the MIDlet
right="$(index t Right)"; fire="$(index t Fire)"; num5="$(index t 'Num 5')"; touch="$(index t Touch)"
box idle t --frames 300 --report 30
check "j2me:runs" "the test MIDlet runs: $(lines "$work/idle.out") reports in 300 frames, $(pictures "$work/idle.out") different pictures (its frame counter and timer are on screen)" \
	cond '[ "$(lines "$work/idle.out")" = 10 ] && [ "$(pictures "$work/idle.out")" -ge 9 ]'

box keys t --frames 300 --report 30 --press "$right":150:30
check "j2me:keys" "Right held from frame 150 moves the square: the reports are the idle run's before frame 150 and differ from it on" \
	same_until "$work/idle.out" "$work/keys.out" 149

box touch t --frames 300 --report 30 --press "$touch":150:20 --axis 0:16384:150:20 --axis 1:16384:150:20
check "j2me:touch" "a touch at frame 150 leaves the MIDlet's mark: the reports are the idle run's before frame 150 and differ from it on" \
	same_until "$work/idle.out" "$work/touch.out" 149

folder q '{"sound":"off"}' "$midlet"
box quiet q --frames 300 --report 30
check "j2me:sound" "the MIDI tune plays: $(sounds "$work/idle.out") different sounds in 10 reports; with Sound off the same reports are $(sounds "$work/quiet.out") sound (silence)" \
	cond '[ "$(sounds "$work/idle.out")" -ge 8 ] && [ "$(sounds "$work/quiet.out")" = 1 ] && [ "$(lines "$work/quiet.out")" = 10 ]'

# the tune is heard within 10 frames of the MIDlet starting it (frame 3):
# with Java's jitter corrector it started at frame 27 (chimera#301)
box start t --frames 60 --report 60 --aud-out "$work/start.raw"
first="$(python3 - "$work/start.raw" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
s = struct.unpack("<%dh" % (len(d) // 2), d)
print(next((f for f in range(len(s) // 1470) if any(s[f * 1470:(f + 1) * 1470])), -1))
PY
)"
check "j2me:sound-start" "the MIDI tune is heard from frame $first (within 10 frames: the synthesizer is paced by the frames alone)" \
	cond '[ "$first" -ge 0 ] && [ "$first" -le 10 ]'

box sfx t --frames 300 --report 30 --press "$num5":150:1 --press "$fire":180:1
check "j2me:effects" "a beep (a WAV clip) on Num 5 at frame 150 and a tone on Fire at 180: the sound is the idle run's before frame 150 and differs from it on" \
	same_until "$work/idle.out" "$work/sfx.out" 149

folder small '{"screenSize":"176x220"}' "$midlet"
box small small --frames 60 --report 30
check "j2me:screen-size" "Screen Size 176x220 gives a 176x220 picture: $(stream "$work/small.out" | awk '{print $6}' | sort -u | tr '\n' ' ')" \
	[ "$(stream "$work/small.out" | awk '{print $6}' | sort -u)" = 176x220 ]

# the MIDlet's Japanese line, at frame 90: the pixels IPAex Gothic draws
# (the font is checked by SHA-256 at build time, so these do not move)
box jp t --frames 90 --report 90 --vid-out "$work/jp90.raw"
jp="$(python3 - "$work/jp90.raw" <<'PY' 2>/dev/null
import sys, hashlib
d = open(sys.argv[1], "rb").read(); nl = d.index(b"\n"); w = int(d[:nl].split()[0]); px = d[nl + 1:]
print(hashlib.sha1(b"".join(px[(y * w) * 4:(y * w + 110) * 4] for y in range(56, 76))).hexdigest())
PY
)"
check "j2me:japanese" "the MIDlet's Japanese line is drawn with the Japanese font: its pixels are the ones IPAex Gothic draws ($jp)" \
	[ "$jp" = 45e873e52c9738ffd85979366fa28a30ef088ea0 ]

# the MIDlet's Chinese line (chimera#326): four characters in GBK bytes,
# read with the machine's text encoding. With Text Encoding GBK they are
# Chinese characters, drawn with Droid Sans Fallback; with the default they
# are four pairs of Latin-1 letters. Big5 (Traditional Chinese) starts.
zh_pixels() { python3 - "$1" <<'PY' 2>/dev/null
import sys, hashlib
d = open(sys.argv[1], "rb").read(); nl = d.index(b"\n"); w = int(d[:nl].split()[0]); px = d[nl + 1:]
print(hashlib.sha1(b"".join(px[(y * w) * 4:(y * w + 110) * 4] for y in range(92, 112))).hexdigest())
PY
}
folder gbk '{"textEncoding":"GBK"}' "$midlet"
box gbk gbk --frames 90 --report 90 --vid-out "$work/gbk90.raw"
zh="$(zh_pixels "$work/gbk90.raw")"
zh_default="$(zh_pixels "$work/jp90.raw")"
check "j2me:chinese" "Text Encoding GBK reads the MIDlet's GBK bytes as Chinese characters, drawn with the Chinese font ($zh); with the default they are Latin-1 letters ($zh_default)" \
	cond '[ "$zh" = e6a0fb7746082d81f53b38cadd380d4731be6153 ] && [ "$zh_default" = b1e4a7c210e8830510395e77978aeea26bfa456f ]'
folder big5 '{"textEncoding":"Big5"}' "$midlet"
box big5 big5 --frames 30 --report 30
check "j2me:big5" "Text Encoding Big5 starts the MIDlet (FreeJ2ME took only three encodings before)" \
	cond '[ "$(lines "$work/big5.out")" -ge 1 ] && ! grep -q "^loadError" "$work/big5.out"'

# ------------------------------------------------- settings, lag and the soundfont (chimera#301)
# A frame is lag when the game gave the screen no new picture in it. The
# MIDlet draws, then sleeps 33 ms: about 30 pictures a second, fewer under a
# lower frame limit. lag <file>: the lag frames at the last report.
lag() { stream "$1" | tail -1 | awk '{print $NF}'; }
folder f15 '{"fpsLimit":"15"}' "$midlet"
folder f2 '{"fpsLimit":"2"}' "$midlet"
box f15 f15 --frames 300 --report 30
box f2 f2 --frames 300 --report 30
check "j2me:lag" "a frame with no new picture is lag: in 300 frames, $(lag "$work/idle.out") at Frame Limit 60 (the MIDlet draws about 30 a second), $(lag "$work/f15.out") at 15, $(lag "$work/f2.out") at 2" \
	cond '[ "$(lag "$work/idle.out")" -ge 100 ] && [ "$(lag "$work/idle.out")" -le 200 ] && [ "$(lag "$work/f15.out")" -ge 200 ] && [ "$(lag "$work/f15.out")" -le 250 ] && [ "$(lag "$work/f2.out")" -ge 280 ] && [ "$(lag "$work/f2.out")" -lt 300 ]'

# the settings FreeJ2ME writes into its own settings file (natively, where
# the files are the work folder's): the project's values, under FreeJ2ME's
# names
if [ -x "$native" ]; then
	folder nset '{"fpsLimit":"7","compatRepaintOnSetCurrent":true,"compatOverridePlatformChecks":false,"m3gPerspectiveCorrection":"on","m3gPerspectiveQuality":"low","dojaVersion":"35"}' "$midlet"
	nat nset nset --frames 5
	conf="$(find "$work/nset" -name game.conf | head -1)"
	check "settings:freej2me" "the project's settings are FreeJ2ME's own: its settings file says $(grep -E '^(fps|compatrepaintonsetcurrent|compatoverrideplatchecks|m3gperspcorrmode|m3gperspcorrsubfactor|dojaversion):' "$conf" 2>/dev/null | sort | tr '\n' ' ')" \
		cond '[ -n "$conf" ] && [ "$(grep -E "^(fps|compatrepaintonsetcurrent|compatoverrideplatchecks|m3gperspcorrmode|m3gperspcorrsubfactor|dojaversion):" "$conf" | sort | tr "\n" " ")" = "compatoverrideplatchecks:off compatrepaintonsetcurrent:on dojaversion:35 fps:7 m3gperspcorrmode:on m3gperspcorrsubfactor:low " ]'
else
	report "settings:freej2me" SKIP "build/bin/run-native is not built (waterbox/build-native.sh)"
fi

# the soundfont: a copy of the sounds Java comes with here, at half the
# volume, so the tune is the same notes and another sound
sf2="$root/build/soundbank/soundbank-emg.sf2"
if [ -f "$sf2" ]; then
	folder sf '{"customSoundfont":true}' "$midlet"
	python3 - "$sf2" "$work/sf/soundfont.sf2" <<'PY'
import struct, sys
d = bytearray(open(sys.argv[1], "rb").read())
def walk(at, end):
    while at + 8 <= end:
        cid, size = d[at:at + 4], struct.unpack_from("<I", d, at + 4)[0]
        if cid == b"LIST":
            walk(at + 12, at + 8 + size)
        elif cid == b"smpl":
            for i in range(at + 8, at + 8 + size - 1, 2):
                struct.pack_into("<h", d, i, struct.unpack_from("<h", d, i)[0] // 2)
        at += 8 + size + (size & 1)
walk(12, len(d))
open(sys.argv[2], "wb").write(d)
PY
	box sf sf --frames 300 --report 30 --state-hash --aud-out "$work/sf.raw"
	box sfrr sf --frames 300 --report 30 --state-hash --rerecord
	box sfdef t --frames 300 --report 300 --aud-out "$work/sfdef.raw"
	# the frames with sound in them, and the silent ones after the first
	gaps() { python3 - "$1" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
s = struct.unpack("<%dh" % (len(d) // 2), d)
peaks = [max(abs(v) for v in s[f * 1470:(f + 1) * 1470]) for f in range(len(s) // 1470)]
first = next((i for i, v in enumerate(peaks) if v), len(peaks))
print(sum(1 for v in peaks if v), sum(1 for v in peaks[first:] if not v))
PY
}
	sfg="$(gaps "$work/sf.raw")"; defg="$(gaps "$work/sfdef.raw")"
	check "j2me:soundfont" "with Use custom MIDI soundfont, the tune plays with the project's soundfont: another sound than Java's own, $(echo "$sfg" | awk '{print $1}') of 300 frames with sound and $(echo "$sfg" | awk '{print $2}') silent once it started (Java's own: $(echo "$defg" | awk '{print $1}') and $(echo "$defg" | awk '{print $2}')), and saved and loaded around every frame it is the straight run" \
		cond '[ "$(echo "$sfg" | awk "{print \$2}")" = 0 ] && [ "$(echo "$sfg" | awk "{print \$1}")" -ge 250 ] && ! cmp -s "$work/sf.raw" "$work/sfdef.raw" && same "$work/sf.out" "$work/sfrr.out"'
else
	report "j2me:soundfont" SKIP "build/soundbank/soundbank-emg.sf2 is not built (waterbox/build-java.sh)"
fi

# ------------------------------------------------- save data and memory
# the MIDlet saves its score in a record store every 30 of its frames, and
# shows at start the score a saved game left
mkdir -p "$work/saved" "$work/again"
box sd t --frames 120 --report 120 --savedata-out "$work/saved"
check "save:export" "Export Save Data gives the MIDlet's record store: $(cd "$work/saved" && find . -type f | sort | tr '\n' ' ')" \
	cond '[ "$(cd "$work/saved" && find rms/Test -name "*.rms" | wc -l)" = 1 ] && [ "$(cd "$work/saved" && find rms/Test -type f | wc -l)" -ge 2 ]'
zipup() { python3 - "$1" "$2" <<'PY'
import os, sys, zipfile
out, root = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w") as z:
    for d, _, fs in sorted(os.walk(root)):
        for f in sorted(fs):
            p = os.path.join(d, f)
            z.write(p, os.path.relpath(p, root).replace(os.sep, "/"))
PY
}
zipup "$work/save.zip" "$work/saved"
folder ld '{}' "$midlet"
cp "$work/save.zip" "$work/ld/"
printf '{"game":["%s"],"savedata":["save.zip"]}' "$(basename "$midlet")" > "$work/ld/slots"
box ld ld --frames 20 --report 20 --savedata-out "$work/again"
box nold t --frames 20 --report 20
# 20 frames is before the MIDlet's first save: the store it exports then can
# only be the one the project brought
check "save:import" "a project started from that save data has the record store from frame 1 ($(cd "$work/again" && find . -type f | wc -l) files, the same as exported) and shows the score it saved (the picture differs from a start with none)" \
	cond '! same "$work/ld.out" "$work/nold.out" && [ "$(lines "$work/ld.out")" = 1 ] && [ "$(cd "$work/again" && find rms/Test -type f 2>/dev/null | wc -l)" -ge 2 ] && diff -r "$work/saved" "$work/again" > /dev/null'
mkdir -p "$work/other/rms/Another Game"
cp "$work/saved/rms/Test/"* "$work/other/rms/Another Game/"
zipup "$work/other.zip" "$work/other"
folder og '{}' "$midlet"
cp "$work/other.zip" "$work/og/"
printf '{"game":["%s"],"savedata":["other.zip"]}' "$(basename "$midlet")" > "$work/og/slots"
box og og --frames 1
check "save:refused" "save data of another game is refused, and says so" grep -q "Init failed.*error.save_other_game" "$work/og.out" "$work/og.err"

# RAM Search's way: the square's x is 100 before Right is held and the same
# number after it is let go; exactly one place in the heap is
for n in 50 95 120; do
	box heap$n t --frames $n --report $n --press "$right":60:30 --dump-domain "Java Heap" "$work/heap$n.bin"
done
found="$(python3 - "$work/heap50.bin" "$work/heap95.bin" "$work/heap120.bin" <<'PY' 2>/dev/null
import struct, sys
a, b, c = (open(p, "rb").read() for p in sys.argv[1:4])
n = len(a) // 4
A, B, C = (struct.unpack("<%di" % n, x) for x in (a, b, c))
hits = [i for i in range(n) if A[i] == 100 and B[i] == C[i] and 100 < B[i] < 240 and (B[i] - 100) % 3 == 0]
print("%d %d %s" % (len(a), len(hits), B[hits[0]] if len(hits) == 1 else "-"))
PY
)"
check "memory:heap" "the Java Heap memory domain ($(echo "$found" | awk '{print $1}') bytes) holds the square's x: $(echo "$found" | awk '{print $2}') place(s) went from 100 to $(echo "$found" | awk '{print $3}') and stayed" \
	cond '[ "$(echo "$found" | awk "{print \$1}")" = 67108864 ] && [ "$(echo "$found" | awk "{print \$2}")" = 1 ]'
box look10 t --frames 300 --report 10
check "memory:looking" "reading the memory three times as often (every 10 frames, not 30) leaves the machine as it was: the reports at frames 30, 60 ... are the same" \
	cond '[ "$(stream "$work/look10.out" | awk "\$2 % 30 == 0")" = "$(stream "$work/idle.out")" ]'

# ------------------------------------------------- the same machine every time
play="--frames 300 --report 10 --press $right:60:30 --press $fire:70:1 --press $num5:100:1 --press $touch:120:5 --axis 0:16384:120:5 --axis 1:16384:120:5"
# shellcheck disable=SC2086
box a t $play --state-hash
# shellcheck disable=SC2086
box b t $play --state-hash
check "box:again" "two runs with keys, a touch and sound end in the same machine: the same 30 reports and the same state ($(awk '/^state/{print $4}' "$work/a.out") bytes)" \
	cond 'same "$work/a.out" "$work/b.out" && grep -q "^state" "$work/a.out" && [ "$(lines "$work/a.out")" = 30 ]'
# shellcheck disable=SC2086
box rr t $play --state-hash --rerecord
check "box:rerecord" "saved and loaded around every frame, the run is the straight run: every report and the state at the end" \
	same "$work/a.out" "$work/rr.out"
# shellcheck disable=SC2086
box ss t $play --state-hash --session
check "box:session" "saved at frame 150 and loaded into a new host, the run is the straight run: every report and the state at the end" \
	same "$work/a.out" "$work/ss.out"
# shellcheck disable=SC2086
box sv t $play --save-at 150 "$work/at150.state"
# shellcheck disable=SC2086
box rs t $play --resume "$work/at150.state" 150 --state-hash
# the runner counts lag frames from its own start, so the resumed run's
# count is compared as the frames since 150 (a frame with no new picture is
# lag)
awk '$1 == "state" || ($1 == "frame" && $2 > 150)' "$work/a.out" | sed 's/ lag [0-9]*$//' > "$work/a-after150.out"
sed 's/ lag [0-9]*$//' "$work/rs.out" > "$work/rs-nolag.out"
lag150="$(stream "$work/a.out" | awk '$2 == 150 {print $NF}')"
check "box:resume" "a state written to a file before frame 150 and loaded by another process: the reports from there, the lag frames since ($(lag "$work/rs.out")) and the state at the end are the straight run's" \
	cond '[ "$(lines "$work/rs.out")" = 15 ] && same "$work/a-after150.out" "$work/rs-nolag.out" && [ "$(lag "$work/rs.out")" = "$(( $(lag "$work/a.out") - lag150 ))" ]'
box rw t --frames 300 --report 5 --press "$right":60:30 --press "$fire":70:1 --rewind
check "box:rewind" "a state kept at frame 150 and loaded at 225: the frames after the load repeat those after the save" \
	repeats "$work/rw.out" 150 225 75 5

# ------------------------------------------------- games, where they are
# Commercial games are not in the repository; these run where
# tests/games-local holds the named files (GAMES_DIR names another folder),
# and say SKIP where it does not. Each is named and checked by its SHA-1.
games="${GAMES_DIR:-$root/tests/games-local}"
game() { # <file> <sha1>: the game's path, or nothing if it is not there as named
	[ -f "$games/$1" ] && [ "$(sha1sum < "$games/$1" | cut -c1-40)" = "$2" ] && echo "$games/$1"
}
for g in "Angry_Birds.jar 4cbb0ee80a3801fef8dcf0fdfda4489662fea9e0 angry-birds" \
	"BaldursGate.jar 85d26a4e06ba1c9f4545c78ee0e186ef72e1422e baldurs-gate" \
	"Cars2.jar 1ed2c3495846bc7c404089dbc03d309d77f34d9b cars-2" \
	"GameofLife.jar 5304706d7495deb6e60a1c2b21d41cd953dbd576 game-of-life"; do
	gfile="${g%% *}"; rest="${g#* }"; gsha="${rest%% *}"; gname="${rest#* }"
	f="$(game "$gfile" "$gsha")"
	if [ -n "$f" ]; then
		folder "$gname" '{}' "$f"
		box "$gname" "$gname" --frames 900 --report 150 --press "$fire":300:3 --press "$fire":450:3 --press "$fire":600:3 --press "$fire":750:3
		gout="$work/$gname.out"
		check "game:$gname" "$gfile starts and draws: $(lines "$gout") reports in 900 frames, $(pictures "$gout") different pictures" \
			cond '[ "$(lines "$gout")" = 6 ] && [ "$(pictures "$gout")" -ge 4 ]'
	else
		report "game:$gname" SKIP "$gfile (sha1 $gsha) is not in $games"
	fi
done
f="$(game Doom.jar 7955b75a4bfd83593938fcf3006a9fa220909666)"
if [ -n "$f" ]; then
	folder doom '{}' "$f"
	# Fire answers its sound question, starts a game and turns its pages
	doom="--frames 1800 --report 30 --press $fire:960:3 --press $fire:1200:3 --press $fire:1350:3 --press $fire:1500:3"
	# shellcheck disable=SC2086
	box doom-a doom $doom --state-hash
	# shellcheck disable=SC2086
	box doom-rr doom $doom --state-hash --rerecord
	check "game:doom-rpg" "Doom RPG (Doom.jar) starts and plays its music once sound is chosen ($(pictures "$work/doom-a.out") pictures, $(sounds "$work/doom-a.out") sounds in 60 reports), and saved and loaded around every one of 1800 frames it is the straight run: every report and the state at the end" \
		cond '[ "$(lines "$work/doom-a.out")" = 60 ] && [ "$(pictures "$work/doom-a.out")" -ge 10 ] && [ "$(sounds "$work/doom-a.out")" -ge 10 ] && same "$work/doom-a.out" "$work/doom-rr.out"'
else
	report "game:doom-rpg" SKIP "Doom.jar (sha1 7955b75a4bfd83593938fcf3006a9fa220909666) is not in $games"
fi

# ------------------------------------------------- refusals
folder none '{}'
box none none --frames 1
check "refused:no-game" "a machine with no game is refused, and says so" grep -q "Init failed.*error.no_game$" "$work/none.out" "$work/none.err"

folder nosf '{"customSoundfont":true}' "$midlet"
box nosf nosf --frames 1
check "refused:no-soundfont" "Use custom MIDI soundfont with no soundfont is refused, and says so" grep -q "Init failed.*error.soundfont_missing" "$work/nosf.out" "$work/nosf.err"
folder badsf '{"customSoundfont":true}' "$midlet"
head -c 4096 /dev/zero > "$work/badsf/soundfont.sf2"
box badsf badsf --frames 1
check "refused:bad-soundfont" "a soundfont that is not a SoundFont 2 file is refused, and says so" grep -q "Init failed.*error.soundfont_unreadable" "$work/badsf.out" "$work/badsf.err"
folder fps1 '{"fpsLimit":"1"}' "$midlet"
box fps1 fps1 --frames 1
check "refused:frame-limit" "a frame limit outside 2 to 80 is refused, and says so" grep -q "Init failed.*error.frame_limit.1" "$work/fps1.out" "$work/fps1.err"

# ------------------------------------------------- the native runner
if [ -x "$native" ]; then
	nat nat t --frames 300 --report 30
	# natively the synthesizer renders on the host's clock: on a slow
	# machine it falls behind and some frames are silent, so sound is only
	# asked to play at all
	check "native:runs" "the native runner runs the MIDlet too: $(lines "$work/nat.out") reports, $(pictures "$work/nat.out") different pictures, $(sounds "$work/nat.out") different sounds (not compared with the sandbox: see the top of this script)" \
		cond '[ "$(lines "$work/nat.out")" = 10 ] && [ "$(pictures "$work/nat.out")" -ge 9 ] && [ "$(sounds "$work/nat.out")" -ge 2 ]'
else
	report "native:runs" SKIP "build/bin/run-native is not built (waterbox/build-native.sh)"
fi

# ------------------------------------------------- Chimera's engine
runner="$chimera_root/build/dll/chimera-run"
package="$chimera_root/build/Cores/freej2me.chimeraCore"
if [ -n "$chimera_root" ] && [ -x "$runner" ] && [ -f "$package" ]; then
	mkdir -p "$work/proj"
	cp "$midlet" "$work/proj/"
	# a project's row N is the runner's frame N + 1
	python3 "$root/tests/make-project.py" "$package" "$work/proj/test.chimeraProject" 200 \
		"press=Right:59:30" "press=Fire:69:1" panel="$work/panel.out" -- game="$work/proj/$(basename "$midlet")" > /dev/null
	"$runner" --project "$work/proj/test.chimeraProject" "$package" --files "$work/proj" --frames 200 \
		--screenshot 120="$work/engine.tga" > "$work/engine.log" 2>&1
	box eng t --frames 121 --report 121 --press "$right":60:30 --press "$fire":70:1 --vid-out "$work/eng.raw"
	check "engine:boot" "a project of the test MIDlet with presses plays in Chimera's headless runner to the sandbox runner's picture at frame 121" \
		python3 - "$work/engine.tga" "$work/eng.raw" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
w, h = struct.unpack("<HH", d[12:16])
px = d[18 + d[0]:]
rows = [px[y * w * 4:(y + 1) * w * 4] for y in range(h)]
if not d[17] & 0x20:
    rows = rows[::-1]
r = open(sys.argv[2], "rb").read()
nl = r.index(b"\n")
sys.exit(0 if r[:nl].split() == [str(w).encode(), str(h).encode()] and b"".join(rows) == r[nl + 1:] else 1)
PY
	# --state makes the loaded machine the one the movie starts from: the
	# rest of the run is a project of its own, from row 0
	python3 "$root/tests/make-project.py" "$package" "$work/proj/long.chimeraProject" 300 \
		"press=Right:59:30" "press=Fire:69:1" panel="$work/panel.out" -- game="$work/proj/$(basename "$midlet")" > /dev/null
	python3 "$root/tests/make-project.py" "$package" "$work/proj/rest.chimeraProject" 150 \
		panel="$work/panel.out" -- game="$work/proj/$(basename "$midlet")" > /dev/null
	"$runner" --project "$work/proj/long.chimeraProject" "$package" --files "$work/proj" --frames 300 \
		--save-state 150="$work/proj/at150.state" --screenshot 291="$work/straight291.tga" --screenshot 299="$work/straight299.tga" > "$work/engine2.log" 2>&1
	"$runner" --project "$work/proj/rest.chimeraProject" "$package" --files "$work/proj" --state "$work/proj/at150.state" --frames 150 \
		--screenshot 140="$work/resumed140.tga" --screenshot 148="$work/resumed148.tga" > "$work/engine3.log" 2>&1
	check "engine:resume" "a state Chimera's headless runner saved at frame 150, loaded by another run of it: frames 140 and 148 after the load are the straight run's frames 291 and 299" \
		cond 'cmp -s "$work/resumed140.tga" "$work/straight291.tga" && cmp -s "$work/resumed148.tga" "$work/straight299.tga" && ! cmp -s "$work/straight291.tga" "$work/straight299.tga"'
	# A screen bigger than the 240x320 the package once declared was cut to
	# its top-left 240x320 in Chimera (chimera#303: EDGE EX at 360x640 showed
	# a quarter of itself). The picture must be the phone's whole screen, the
	# sandbox runner's own at frame 121.
	folder big '{"screenSize":"360x640"}' "$midlet"
	python3 "$root/tests/make-project.py" "$package" "$work/proj/big.chimeraProject" 130 screenSize=360x640 \
		"press=Right:59:30" panel="$work/panel.out" -- game="$work/proj/$(basename "$midlet")" > /dev/null
	"$runner" --project "$work/proj/big.chimeraProject" "$package" --files "$work/proj" --frames 130 \
		--screenshot 120="$work/big.tga" > "$work/big-engine.log" 2>&1
	box bigb big --frames 121 --report 121 --press "$right":60:30 --vid-out "$work/big.raw"
	check "engine:screen-size" "at Screen Size 360x640 Chimera's picture is the whole 360x640 screen, the sandbox runner's own at frame 121" \
		python3 - "$work/big.tga" "$work/big.raw" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
w, h = struct.unpack("<HH", d[12:16])
px = d[18 + d[0]:]
rows = [px[y * w * 4:(y + 1) * w * 4] for y in range(h)]
if not d[17] & 0x20:
    rows = rows[::-1]
r = open(sys.argv[2], "rb").read()
nl = r.index(b"\n")
sys.exit(0 if (w, h) == (360, 640) and r[:nl].split() == [b"360", b"640"] and b"".join(rows) == r[nl + 1:] else 1)
PY
	# the soundfont as the project's firmware: Chimera mounts it under its id
	# and the machine is the sandbox runner's with the same file (without the
	# file the core refuses to start, and there is no picture)
	if [ -f "$work/sf/soundfont.sf2" ]; then
		mkdir -p "$work/projsf"
		cp "$midlet" "$work/projsf/"
		python3 "$root/tests/make-project.py" "$package" "$work/projsf/sf.chimeraProject" 200 \
			customSoundfont=true firmware=soundfont.sf2="$work/sf/soundfont.sf2" panel="$work/panel.out" \
			-- game="$work/projsf/$(basename "$midlet")" > /dev/null
		"$runner" --project "$work/projsf/sf.chimeraProject" "$package" --files "$work/projsf" \
			--firmware soundfont.sf2="$work/sf/soundfont.sf2" --frames 200 --screenshot 120="$work/enginesf.tga" > "$work/enginesf.log" 2>&1
		box engsf sf --frames 121 --report 121 --vid-out "$work/engsf.raw"
		check "engine:soundfont" "a project with Use custom MIDI soundfont and the soundfont as its firmware plays in Chimera's headless runner to the sandbox runner's picture at frame 121" \
			python3 - "$work/enginesf.tga" "$work/engsf.raw" <<'PY'
import struct, sys
try:
    d = open(sys.argv[1], "rb").read()
except OSError:
    sys.exit(1)
w, h = struct.unpack("<HH", d[12:16])
px = d[18 + d[0]:]
rows = [px[y * w * 4:(y + 1) * w * 4] for y in range(h)]
if not d[17] & 0x20:
    rows = rows[::-1]
r = open(sys.argv[2], "rb").read()
nl = r.index(b"\n")
sys.exit(0 if r[:nl].split() == [str(w).encode(), str(h).encode()] and b"".join(rows) == r[nl + 1:] else 1)
PY
	else
		report "engine:soundfont" SKIP "no soundfont was made (build/soundbank/soundbank-emg.sf2 is not built)"
	fi
else
	report "engine:boot" SKIP "needs CHIMERA_ROOT with build/dll/chimera-run and build/Cores/freej2me.chimeraCore"
	report "engine:resume" SKIP "needs CHIMERA_ROOT with build/dll/chimera-run and build/Cores/freej2me.chimeraCore"
	report "engine:screen-size" SKIP "needs CHIMERA_ROOT with build/dll/chimera-run and build/Cores/freej2me.chimeraCore"
	report "engine:soundfont" SKIP "needs CHIMERA_ROOT with build/dll/chimera-run and build/Cores/freej2me.chimeraCore"
fi

echo
echo "$ok passed, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
