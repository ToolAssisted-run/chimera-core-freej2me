#!/bin/bash
# The package in Chimera's own window (Chimera.exe under Mono, on a private
# Xvfb display), which the headless runner cannot show:
#  - the test MIDlet runs in the window, with keys pressed, and leaves the
#    Java heap the headless runner leaves at the same frame, at 240x320;
#  - the keys the package ships become Chimera's keys for the controls;
#  - Doom RPG, where tests/games-local (or GAMES_DIR) has it, plays in the
#    window to the headless runner's heap.
#
# Usage: tests/run-frontend.sh --chimera-root <chimera checkout> [--frames N] [--keep]
#   --keep   leave the work folder (logs, configurations, projects) and say where it is
# Needs: mono, Xvfb, Chimera built with its window (dotnet build), the
# package installed in that checkout (waterbox/build-package.sh -r <chimera>),
# and the test MIDlet (tests/midlet/build.sh). (From chimera-core-tic80.)
# SPDX-License-Identifier: MIT
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
wb="$root/waterbox"
frames=300
chimera_root=""
keep=""
while [ $# -gt 0 ]; do
	case "$1" in
		--keep) keep=1 ;;
		--chimera-root) chimera_root="$2"; shift ;;
		--frames) frames="$2"; shift ;;
		-*) echo "unknown option: $1" >&2; exit 2 ;;
		*) break ;;
	esac
	shift
done
[ -n "$chimera_root" ] && [ -d "$chimera_root" ] || { echo "say where Chimera is: --chimera-root <chimera checkout>" >&2; exit 2; }
chimera_root="$(cd "$chimera_root" && pwd)"

emu_exe="$chimera_root/build/Chimera.exe"
package="$chimera_root/build/Cores/freej2me.chimeraCore"
runwbx="$root/build/bin/run-wbx"
core="$root/build/guest/core.wbx"
midlet="$root/build/tests/midlet/test-midlet.jar"
runner="$chimera_root/build/dll/chimera-run"
[ -f "$emu_exe" ] || { echo "Chimera's window is not built: $emu_exe" >&2; exit 1; }
[ -f "$package" ] || { echo "the package is not installed: $package (run waterbox/build-package.sh -r $chimera_root)" >&2; exit 1; }
[ -x "$runwbx" ] && [ -f "$core" ] && [ -f "$midlet" ] || { echo "the sandbox build or the test MIDlet is not there: $runwbx, $core, $midlet" >&2; exit 1; }
[ -x "$runner" ] || { echo "Chimera's headless runner is not built: $runner" >&2; exit 1; }

work="$(mktemp -d)"
xvfb_pid=""
cleanup() {
	[ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null
	if [ -n "$keep" ]; then echo "work folder kept: $work"; else rm -rf "$work"; fi
}
trap cleanup EXIT

export LD_LIBRARY_PATH="$chimera_root/build/dll:$chimera_root/build:/usr/lib/x86_64-linux-gnu"
export MONO_CRASH_NOFILE=1 MONO_WINFORMS_XIM_STYLE=disabled ALSOFT_DRIVERS=null
# always a display of this test's own: nobody's desktop is drawn on
command -v Xvfb >/dev/null || { echo "Xvfb not found (apt install xvfb)" >&2; exit 1; }
for n in 90 91 92 93 94 95 96; do
	if [ ! -e "/tmp/.X11-unix/X$n" ]; then
		Xvfb ":$n" -screen 0 1024x768x24 -nolisten tcp > /dev/null 2>&1 & xvfb_pid=$!
		export DISPLAY=":$n"; break
	fi
done
[ -n "$xvfb_pid" ] || { echo "no free display number for Xvfb" >&2; exit 1; }
sleep 1

config="$work/config.ini"
( cd "$chimera_root" && timeout 120 mono "$emu_exe" --headless "--config=$config" "--lua=$here/exit.lua" ) > "$work/bootstrap.log" 2>&1
[ -f "$config" ] || { echo "Chimera did not write a configuration:"; tail -5 "$work/bootstrap.log"; exit 1; } >&2
sed -i 's/"DispMethod": [0-9]/"DispMethod": 1/' "$config"

ok=0
failed=0
skipped=0
report() {
	printf '%-16s %-5s %s\n' "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok + 1)) ;; SKIP) skipped=$((skipped + 1)) ;; *) failed=$((failed + 1)) ;; esac
}

# make_project <tag> <frames> <game> [press=...]...: a project with the game
# beside it, where Chimera looks, and a movie of that many frames
make_project() {
	local tag="$1" nframes="$2" gamefile="$3"; shift 3
	local dir="$work/$tag"
	mkdir -p "$dir"
	cp "$gamefile" "$dir/"
	python3 "$here/make-project.py" "$package" "$dir/$tag.chimeraProject" "$nframes" "panel=$work/panel.out" "$@" \
		-- "game=$dir/$(basename "$gamefile")" > "$work/$tag.log" 2>&1
}
# run_frontend <tag> <configuration> <frames> <domain or "">: the project in the window
run_frontend() {
	local tag="$1" cfg="$2" nframes="$3" domain="$4" job="$work/job.$1.txt"
	{
		echo "frames=$nframes"
		echo "meta=$work/$tag.meta.txt"
		echo "domain=$domain"
	} > "$job"
	( cd "$chimera_root" && MINIHAWK_JOB="$job" timeout 900 mono "$emu_exe" --headless \
		"--config=$cfg" "--project=$work/$tag/$tag.chimeraProject" "--lua=$here/frontend-run.lua" ) >> "$work/$tag.log" 2>&1
	[ -f "$work/$tag.meta.txt" ] && grep -q "^status=OK" "$work/$tag.meta.txt"
}
value() { sed -n "s/^$2=//p" "$work/$1.meta.txt" 2>/dev/null; }
# the SHA-256 of a file, as Chimera's Lua memory.hash_region writes it
sha() { sha256sum < "$1" 2>/dev/null | cut -c1-64 | tr a-f A-F; }

python3 "$here/settings-config.py" "$config" "$work/config.base.ini" '{}'

mkdir -p "$work/probe"
cp "$midlet" "$work/probe/"
printf '{"game":["%s"]}' "$(basename "$midlet")" > "$work/probe/slots"; printf '{}' > "$work/probe/settings"
"$runwbx" "$core" "$work/probe" --frames 1 --list-panel > "$work/panel.out" 2> /dev/null

# ---- the test MIDlet: needs no file of anyone's. The movie is longer than
# the run (a window whose movie ends stops there); Right is held from frame
# 60 (row 59) for 30 frames, Fire pressed at 70, Num 5 at 100
make_project tm $((frames + 100)) "$midlet" "press=Right:59:30" "press=Fire:69:1" "press=Num 5:99:1"
"$runner" --project "$work/tm/tm.chimeraProject" "$package" --files "$work/tm" --frames "$frames" \
	--dump "Java Heap=$work/engine.heap.bin" > "$work/engine.log" 2>&1
want="$(sha "$work/engine.heap.bin")"
if ! run_frontend tm "$work/config.base.ini" "$frames" "Java Heap"; then
	report "window:midlet" FAIL "Chimera did not finish the run: $(value tm detail) $(tail -2 "$work/tm.log" | tr '\n' ' ' | cut -c1-200)"
elif [ "$(value tm frames)" != "$frames" ] || [ "$(value tm width)" != 240 ] || [ "$(value tm height)" != 320 ]; then
	report "window:midlet" FAIL "frames $(value tm frames) of $frames, picture $(value tm width)x$(value tm height) and not 240x320"
elif [ -s "$work/engine.heap.bin" ] && [ "$(value tm domainhash | tr a-f A-F)" = "$want" ]; then
	report "window:midlet" PASS "the test MIDlet ran $frames frames in Chimera's window, 240x320, with Right, Fire and Num 5 pressed; its Java heap ($(value tm domainsize) bytes) is what the headless runner leaves"
else
	report "window:midlet" FAIL "the Java heap differs from the headless runner's (window $(value tm domainhash), headless $want)"
fi

python3 "$here/forget-controller.py" "$work/config.base.ini" "$work/config.keys.ini" "J2ME Controls"
mkdir -p "$work/keys"; cp "$work/tm/$(basename "$midlet")" "$work/keys/"
sed 's/"freej2me-test-[^"]*"/"freej2me-test-keys"/' "$work/tm/tm.chimeraProject" > "$work/keys/keys.chimeraProject"
if run_frontend keys "$work/config.keys.ini" 1 ""; then
	if python3 "$here/check-keybinds.py" "$work/config.keys.ini" "$wb/default_keybinds.json" "J2ME Controls" "Fire" > "$work/keys.txt" 2>&1; then
		report "window:keys" PASS "$(cat "$work/keys.txt")"
	else
		report "window:keys" FAIL "$(head -1 "$work/keys.txt")"
	fi
else
	report "window:keys" FAIL "Chimera did not finish the run: $(tail -2 "$work/keys.log" | tr '\n' ' ' | cut -c1-200)"
fi

# ---- Doom RPG, where it is: through its sound question into the game
games="${GAMES_DIR:-$root/tests/games-local}"
doom="$games/Doom.jar"
if [ -f "$doom" ] && [ "$(sha1sum < "$doom" | cut -c1-40)" = 7955b75a4bfd83593938fcf3006a9fa220909666 ]; then
	dframes=1500
	make_project doom $((dframes + 100)) "$doom" "press=Fire:959:3" "press=Fire:1199:3" "press=Fire:1349:3"
	"$runner" --project "$work/doom/doom.chimeraProject" "$package" --files "$work/doom" --frames "$dframes" \
		--dump "Java Heap=$work/doom-engine.heap.bin" > "$work/doom-engine.log" 2>&1
	want="$(sha "$work/doom-engine.heap.bin")"
	if ! run_frontend doom "$work/config.base.ini" "$dframes" "Java Heap"; then
		report "window:doom" FAIL "Chimera did not finish the run: $(value doom detail) $(tail -2 "$work/doom.log" | tr '\n' ' ' | cut -c1-200)"
	elif [ "$(value doom frames)" = "$dframes" ] && [ -s "$work/doom-engine.heap.bin" ] && [ "$(value doom domainhash | tr a-f A-F)" = "$want" ]; then
		report "window:doom" PASS "Doom RPG ran $dframes frames in the window, through its sound question into the game, and left the Java heap the headless runner leaves"
	else
		report "window:doom" FAIL "frames $(value doom frames) of $dframes, or the Java heap differs from the headless runner's"
	fi
else
	report "window:doom" SKIP "Doom.jar (sha1 7955b75a4bfd83593938fcf3006a9fa220909666) is not in $games"
fi

echo
echo "$ok passed, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
