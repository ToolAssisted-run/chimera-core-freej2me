#!/bin/sh
# Builds the core's package and installs it into a Chimera checkout as
# build/Cores/freej2me.chimeraCore.
#
# A package is the sandboxed core (core.wbx), what the frontend is told about
# it (waterbox.config, file_slots.json, default_keybinds.json), and the
# licences of everything compiled into it.
#
# Usage: waterbox/build-package.sh -r <chimera checkout> [-m <miniBox checkout>] [-j JOBS]
# miniBox is taken from the Chimera checkout unless -m (or MINIBOX_DIR) says
# otherwise. It must be built, with its C++ guest toolchain (docs/BUILDING.md).
# Every step is built when it is missing: OpenJDK (build-jdk.sh), the Java
# half (build-java.sh), the guest (build-guest.sh).
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-}"
chimera_root=""
jobs="$(nproc)"
while getopts "m:r:j:" opt; do
	case "$opt" in
		m) mb="$OPTARG" ;;
		r) chimera_root="$OPTARG" ;;
		j) jobs="$OPTARG" ;;
		*) exit 2 ;;
	esac
done

[ -n "$chimera_root" ] && [ -d "$chimera_root" ] || {
	echo "say where Chimera is: -r <chimera checkout>" >&2; exit 1; }
chimera_root="$(cd "$chimera_root" && pwd)"
[ -n "$mb" ] || mb="$chimera_root/extern/chimera-common-minibox"

sh "$here/apply-patches.sh"
[ -f "$root/build/jdk/images/jdk/bin/jlink" ] && [ -f "$root/build/jdk/compile_commands.json" ] || sh "$here/build-jdk.sh" -j "$jobs"
sh "$here/build-java.sh"
sh "$here/build-guest.sh" -m "$mb" -j "$jobs"

staging="$root/build/package-staging"
rm -rf "$staging"
mkdir -p "$staging"
cp "$root/build/guest/core.wbx" "$staging/core.wbx"
cp "$here/waterbox.config" "$staging/waterbox.config"
cp "$here/default_keybinds.json" "$staging/default_keybinds.json"
# The core-declared file form for the project wizard (slots, cardinality,
# formats, tooltips) - the frontend renders it, this file decides it.
cp "$here/file_slots.json" "$staging/file_slots.json"
# the terms travel with the binary: this package may be downloaded on its
# own, and the emulator inside it is somebody else's work under somebody
# else's licence (see waterbox/package-licenses.json)
python3 "$mb/source/guest/package-licenses.py" "$root" "$staging"

# ---- version (see chimera docs: commit-as-version, stamped by CD) ----
core_version="${CORE_VERSION:-}"
if [ -z "$core_version" ]; then
	if commit="$(git -C "$root" rev-parse --short=12 HEAD 2>/dev/null)"; then
		git -C "$root" diff --quiet HEAD 2>/dev/null || commit="$commit-dirty"
		core_version="$commit+local"
	else
		core_version="unversioned+local"
	fi
fi
# When that commit was made, beside it (chimera issue #67): a commit says which
# version this is and nothing about which of two is newer. The COMMIT's date, in
# UTC, and never the build's: the same commit must still make the same package.
core_version_date="$(TZ=UTC git -C "$root" log -1 --date=format-local:%Y-%m-%dT%H:%M:%SZ --format=%cd HEAD 2>/dev/null || true)"
python3 - "$staging/waterbox.config" "$core_version" "$core_version_date" <<'PYVER'
import json, sys
path, version, date = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path) as f:
    cfg = json.load(f)
cfg["version"] = version
if date:
    cfg["versionDate"] = date
with open(path, "w") as f:
    json.dump(cfg, f, indent=2)
    f.write("\n")
PYVER

# ---- provenance: what built this exact package (inputs only) ----
gccver="$(gcc -dumpfullversion)"
musl_version="$(cat "$mb/extern/musl/VERSION" 2>/dev/null || echo unknown)"
binutils_version="$(ld --version | head -1 | grep -o '[0-9][0-9.]*$' || echo unknown)"
os_id="$(. /etc/os-release 2>/dev/null && printf '%s %s' "${ID:-unknown}" "${VERSION_ID:-}" || echo unknown)"
guest_kit="$(git -C "$mb" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
fj_pin="$(git -C "$root/extern/freej2me-plus" rev-parse HEAD 2>/dev/null || echo unknown)"
jdk_pin="$(git -C "$root/extern/jdk21u" rev-parse HEAD 2>/dev/null || echo unknown)"
python3 - "$staging/build.json" <<PYPROV
import json, subprocess
def git(*args, default="unknown"):
    try:
        return subprocess.run(["git", "-C", "$root", *args], capture_output=True,
                              text=True, check=True).stdout.strip()
    except Exception:
        return default
json.dump({
    "version": "$core_version",
    "source": {"commit": git("rev-parse", "HEAD"),
               "origin": git("config", "--get", "remote.origin.url", default=""),
               "dirty": "-dirty" in "$core_version"},
    "toolchain": {"compiler": "gcc $gccver", "binutils": "$binutils_version",
                  "target": "x86_64-linux-musl", "musl": "$musl_version"},
    "guestKit": {"name": "miniBox", "commit": "$guest_kit"},
    "upstream": {"name": "FreeJ2ME-Plus", "pin": "$fj_pin", "vm": {"name": "OpenJDK 21 (jdk21u)", "pin": "$jdk_pin"}},
    "builtOn": "$os_id",
}, open("$staging/build.json", "w"), indent=2, sort_keys=True)
PYPROV

cores_dir="$chimera_root/build/Cores"
mkdir -p "$cores_dir"
zip_path="$cores_dir/freej2me.chimeraCore"
rm -f "$zip_path"
# deterministic packaging: sorted entries, fixed timestamp/permissions, pinned
# compression - the package's SHA1 is the core's identity (movies cite it)
python3 - "$staging" "$zip_path" <<'PYEOF'
import hashlib, os, sys, tempfile, zipfile

staging, zip_path = sys.argv[1], sys.argv[2]
FIXED_DATE = (1980, 1, 1, 0, 0, 0)

def write_package(path):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for root, dirs, files in os.walk(staging):
            dirs.sort()
            for name in sorted(files):
                full = os.path.join(root, name)
                info = zipfile.ZipInfo(os.path.relpath(full, staging), date_time=FIXED_DATE)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = 0o644 << 16
                with open(full, "rb") as f:
                    z.writestr(info, f.read())

write_package(zip_path)
with tempfile.NamedTemporaryFile(suffix=".zip") as tmp:
    write_package(tmp.name)
    again = hashlib.sha1(open(tmp.name, "rb").read()).hexdigest()
first = hashlib.sha1(open(zip_path, "rb").read()).hexdigest()
if first != again:
    sys.exit(f"packaging is not deterministic: {first} then {again}")
print(f"package sha1 {first}")
PYEOF

for cache in "$chimera_root"/build/CoreCache/freej2me-*; do
	[ -d "$cache" ] && rm -rf "$cache" || true
done
echo "packaged -> $zip_path"
