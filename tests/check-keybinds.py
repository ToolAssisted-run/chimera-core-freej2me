#!/usr/bin/env python3
# Checks that the bindings the package ships became Chimera's for its panel.
# Chimera keeps bindings for the controls the running game has, so every
# control it has a binding entry for must carry the package's, and the
# control named on the command line must be among them.
#
# Usage: check-keybinds.py <configuration written by Chimera> <package default_keybinds.json> <panel> <a control the game has>
import json
import sys

cfg = json.load(open(sys.argv[1]))
shipped = json.load(open(sys.argv[2]))
name, must = sys.argv[3], sys.argv[4]

want = shipped["AllTrollers"][name]
got = cfg.get("AllTrollers", {}).get(name)
if got is None:
    sys.exit(f"Chimera has no bindings at all for '{name}'")
if must not in got:
    sys.exit(f"Chimera has no binding entry for '{must}'")
unknown = [k for k in got if k not in want]
if unknown:
    sys.exit(f"Chimera has controls the package does not declare: {unknown[:5]}")
differing = {k: (want[k], v) for k, v in got.items() if want[k] != v}
if differing:
    sys.exit(f"bindings differ from the package's: {dict(list(differing.items())[:5])}")
print(f"{name}: the {len(got)} controls Chimera lists carry the package's bindings ('{must}' is {got[must] or 'unbound'})")
