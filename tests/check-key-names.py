#!/usr/bin/env python3
"""Checks that every key the package's default bindings name is a key
Chimera knows, so that none ships unbound by a misspelling. Chimera's names
are read from its source: the keyboard's from the window's key tables
(source/ui/qt/key_codes.inc, every system's), Shift, Ctrl and Alt for both
keys of each, as Chimera merges them by default (a panel that binds
LeftShift or RightShift must bind Shift too), and the mouse's. A gamepad's
buttons are not checked.

usage: check-key-names.py <chimera checkout> <default_keybinds.json>"""
import json
import re
import sys

chimera, binds = sys.argv[1], sys.argv[2]
tables = open(chimera + "/source/ui/qt/key_codes.inc").read()
known = set(re.findall(r'\{\s*0x[0-9A-Fa-f]+\s*,\s*"([^"]+)"\s*\}', tables))
if not known:
    sys.exit("no key names found in " + chimera + "/source/ui/qt/key_codes.inc")
# Chimera merges the left and right modifier keys unless told not to (on by
# default): a press of either Shift arrives as "Shift". LeftShift and
# RightShift fire only with the merging off, so a panel that binds them must
# also bind the merged key, or Shift does nothing on a default setup.
merged = {"LeftShift": "Shift", "RightShift": "Shift", "LeftCtrl": "Ctrl", "RightCtrl": "Ctrl",
          "LeftAlt": "Alt", "RightAlt": "Alt"}
known |= set(merged.values())
known |= {"WMouse L", "WMouse R", "WMouse M", "WMouse 1", "WMouse 2", "WMouse X", "WMouse Y", "RMouse X", "RMouse Y"}
cfg = json.load(open(binds))
bad = []
for name, panel in cfg.get("AllTrollers", {}).items():
    parts = {p.strip() for v in panel.values() for p in v.split(",")}
    for side, both in merged.items():
        if side in parts and both not in parts:
            bad.append(f"{name}: {side} is bound but {both} is not (a default setup sends only {both})")
    for control, value in panel.items():
        for key in filter(None, (v.strip() for v in value.split(","))):
            # a chord is keys joined by +
            for part in key.split("+"):
                # a gamepad's buttons (J1 B1, X1 DpadUp) are named by the pad
                if re.match(r"[JX]\d+ ", part):
                    continue
                if part not in known:
                    bad.append(f"{control}: {part}")
for panel in cfg.get("AllTrollersAnalog", {}).values():
    for control, value in panel.items():
        for axis in filter(None, (v.strip() for v in (value.get("Value") or "").split(","))):
            if not re.match(r"[JX]\d+ ", axis) and axis not in known:
                bad.append(f"{control}: {axis}")
if bad:
    sys.exit("keys Chimera does not know: " + ", ".join(bad))
print(f"every key the bindings name is one of Chimera's ({len(known)} names)")
