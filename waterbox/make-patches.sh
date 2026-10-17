#!/bin/sh
# Writes patches/ from the patched submodules: the reverse of
# apply-patches.sh. A patch is a submodule's difference in a list of files.
# Edit the tree, run this, and apply-patches.sh must then say "already
# applied". A file is in ONE patch, whole.
set -eu
trap '' PIPE
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"

# submodule | patch name | files (space separated), in the order they apply
series='
jdk21u|0001-linked-in-libraries-are-found|src/hotspot/share/runtime/os.cpp
jdk21u|0002-zero-pays-for-the-code-it-runs|src/hotspot/share/interpreter/zero/bytecodeInterpreter.cpp
freej2me-plus|0001-a-soundfont-is-read-whole|src/javax/microedition/media/Manager.java
freej2me-plus|0002-tickers-and-gauges-move-above-60|src/javax/microedition/lcdui/Ticker.java src/javax/microedition/lcdui/Gauge.java
'

for sub in jdk21u freej2me-plus libffi; do
	tree="$root/extern/$sub"
	pdir="$root/patches/$sub"
	# a patch that comes out the same is left as it is, so that a build is
	# not taken for older than its patches
	keep="$(mktemp -d)"
	printf '%s\n' "$series" | grep "^$sub|" | while IFS='|' read -r _ name files; do
		mkdir -p "$pdir"
		# shellcheck disable=SC2086
		git -C "$tree" diff -- $files > "$keep/$name.patch"
		cmp -s "$keep/$name.patch" "$pdir/$name.patch" 2> /dev/null || cp "$keep/$name.patch" "$pdir/$name.patch"
		echo "$sub/$name.patch: $(grep -c '^diff --git' "$pdir/$name.patch") files"
	done
	# a patch the series no longer names goes
	for p in "$pdir"/*.patch; do
		[ -e "$p" ] || continue
		[ -e "$keep/$(basename "$p")" ] || rm -f "$p"
	done
	rm -r "$keep"
	# a changed file no patch names would be lost by a fresh clone
	left="$(git -C "$tree" status --porcelain --untracked-files=no | wc -l)"
	named=0
	ls "$pdir"/*.patch > /dev/null 2>&1 && named="$(cat "$pdir"/*.patch | grep -c '^diff --git')"
	[ "$left" -eq "$named" ] || { echo "extern/$sub has $left changed files and the patches name $named: add the rest to the series" >&2; exit 1; }
done
