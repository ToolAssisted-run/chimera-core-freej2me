#!/bin/sh
# Applies the numbered patches in patches/<submodule>/ to each submodule in
# extern/. Idempotent: a tree that already carries its whole series is left
# alone, a pristine one has it applied, and anything in between is an error
# that names the files. (From chimera-core-tic80.)
#
# A series is judged as a whole. Judging a patch at a time - does it apply,
# else does it reverse - cannot work once two patches touch the same place,
# and the cheaper tests ("the tree has changes", "a file holds a marker")
# cannot see a tree where one file was reverted by hand. So what the series
# should leave behind is worked out on a scratch copy - the touched files as
# the submodule's HEAD has them, with every patch applied in order - and the
# working tree is compared with that.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"

apply_series() {
	sub="$1"
	tree="$root/extern/$sub"
	pdir="$root/patches/$sub"
	ls "$pdir"/*.patch > /dev/null 2>&1 || { echo "$sub: no patches to apply"; return 0; }
	if [ "$(git -C "$tree" rev-parse --show-toplevel 2>/dev/null)" != "$(cd "$tree" 2>/dev/null && pwd -P)" ]; then
		echo "extern/$sub is not checked out; run:" >&2
		echo "  git -C $root submodule update --init extern/$sub" >&2
		exit 1
	fi

	scratch="$(mktemp -d)"
	# every file the series touches (a created or deleted file has /dev/null on one side)
	cat "$pdir"/*.patch | sed -n 's#^--- a/##p; s#^+++ b/##p' | sort -u > "$scratch/touched"

	# the scratch copy: those files as HEAD has them (one the series creates is absent)
	mkdir "$scratch/tree"
	while IFS= read -r f; do
		if git -C "$tree" cat-file -e "HEAD:$f" 2>/dev/null; then
			mkdir -p "$scratch/tree/$(dirname "$f")"
			git -C "$tree" show "HEAD:$f" > "$scratch/tree/$f"
			case "$(git -C "$tree" ls-tree HEAD -- "$f")" in 100755*) chmod +x "$scratch/tree/$f" ;; esac
		fi
	done < "$scratch/touched"

	pristine=1
	while IFS= read -r f; do
		if [ -e "$scratch/tree/$f" ]; then
			cmp -s "$scratch/tree/$f" "$tree/$f" || { pristine=0; break; }
		elif [ -e "$tree/$f" ]; then
			pristine=0; break
		fi
	done < "$scratch/touched"

	# the series is tried on the scratch copy first, whatever state the tree is in
	for p in "$pdir"/*.patch; do
		(cd "$scratch/tree" && git apply "$p") || {
			echo "$sub: the series does not apply to the submodule's HEAD at $(basename "$p") - was the submodule moved without rebasing the patches?" >&2
			rm -rf "$scratch"
			exit 1
		}
	done

	if [ "$pristine" -eq 1 ]; then
		for p in "$pdir"/*.patch; do
			git -C "$tree" apply "$p"
			echo "$sub: applied $(basename "$p")"
		done
		rm -rf "$scratch"
		return 0
	fi

	# not pristine: it must then be exactly what the whole series leaves behind
	wrong=0
	while IFS= read -r f; do
		if [ -e "$scratch/tree/$f" ]; then
			cmp -s "$scratch/tree/$f" "$tree/$f" 2>/dev/null || { echo "$sub: not as the series leaves it: $f" >&2; wrong=1; }
		elif [ -e "$tree/$f" ]; then
			echo "$sub: the series deletes this, and it is there: $f" >&2; wrong=1
		fi
	done < "$scratch/touched"
	rm -rf "$scratch"
	if [ "$wrong" -ne 0 ]; then
		echo "extern/$sub is partly patched. To start again from the submodule's HEAD:" >&2
		echo "  git -C extern/$sub reset --hard && waterbox/apply-patches.sh" >&2
		echo "(that discards edits made in the tree - turn them into a patch first)" >&2
		exit 1
	fi
	echo "$sub: already applied, all $(ls "$pdir"/*.patch | wc -l) patches"
}

for sub in jdk21u freej2me-plus libffi; do
	apply_series "$sub"
done
