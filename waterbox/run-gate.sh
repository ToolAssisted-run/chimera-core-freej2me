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
# WHAT RUNS WITH NO FILES OF YOURS: everything. The test MIDlet
# (tests/midlet) is this repository's own: a square moved by the keys, a
# timer, text, a record store, a touch mark, a MIDI tune, a tone and a beep.
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
for src in "$here"/*.c "$here"/*.h "$here"/pack.S "$root"/patches/*/*.patch $(find "$root/java" -name '*.java'); do
	[ -e "$src" ] || continue
	[ "$core" -nt "$src" ] || stale="$stale $(basename "$src")"
done
check "package:fresh" "core.wbx is not older than its sources${stale:+ (older than:$stale)}" [ -z "$stale" ]

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

if [ -n "$chimera_root" ] && [ -f "$chimera_root/source/gui/Chimera.Input/KeyMouseInput/DistinctKey.cs" ]; then
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

box sfx t --frames 300 --report 30 --press "$num5":150:1 --press "$fire":180:1
check "j2me:effects" "a beep (a WAV clip) on Num 5 at frame 150 and a tone on Fire at 180: the sound is the idle run's before frame 150 and differs from it on" \
	same_until "$work/idle.out" "$work/sfx.out" 149

folder small '{"screenSize":"176x220"}' "$midlet"
box small small --frames 60 --report 30
check "j2me:screen-size" "Screen Size 176x220 gives a 176x220 picture: $(stream "$work/small.out" | awk '{print $6}' | sort -u | tr '\n' ' ')" \
	[ "$(stream "$work/small.out" | awk '{print $6}' | sort -u)" = 176x220 ]

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
box rw t --frames 300 --report 5 --press "$right":60:30 --press "$fire":70:1 --rewind
check "box:rewind" "a state kept at frame 150 and loaded at 225: the frames after the load repeat those after the save" \
	repeats "$work/rw.out" 150 225 75 5

# ------------------------------------------------- refusals
folder none '{}'
box none none --frames 1
check "refused:no-game" "a machine with no game is refused, and says so" grep -q "Init failed.*no game was given" "$work/none.out" "$work/none.err"

# ------------------------------------------------- the native runner
if [ -x "$native" ]; then
	nat nat t --frames 300 --report 30
	check "native:runs" "the native runner runs the MIDlet too: $(lines "$work/nat.out") reports, $(pictures "$work/nat.out") different pictures, $(sounds "$work/nat.out") different sounds (not compared with the sandbox: see the top of this script)" \
		cond '[ "$(lines "$work/nat.out")" = 10 ] && [ "$(pictures "$work/nat.out")" -ge 9 ] && [ "$(sounds "$work/nat.out")" -ge 8 ]'
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
else
	report "engine:boot" SKIP "needs CHIMERA_ROOT with build/dll/chimera-run and build/Cores/freej2me.chimeraCore"
fi

echo
echo "$ok passed, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
