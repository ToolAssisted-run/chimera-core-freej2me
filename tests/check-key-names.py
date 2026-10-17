#!/usr/bin/env python3
"""Checks that every key the package's default bindings name is a key
Chimera knows, so that none ships unbound by a misspelling. Chimera's names
are read from its source: the keyboard's (source/gui/Chimera.Input/
KeyMouseInput/DistinctKey.cs, with the names DistinctKeyNameOverrides.cs
gives some of them, and Shift, Ctrl and Alt for both keys of each, as
Chimera merges them by default: a panel that binds LeftShift or RightShift
must bind Shift too) and the mouse's. A gamepad's buttons are not checked.

usage: check-key-names.py <chimera checkout> <default_keybinds.json>"""
import json
import re
import sys

chimera, binds = sys.argv[1], sys.argv[2]
gui = chimera + "/source/gui"
enum = open(gui + "/Chimera.Input/KeyMouseInput/DistinctKey.cs").read()
enum = re.sub(r"///.*", "", enum)
body = enum[enum.index("enum DistinctKey"):]
body = body[body.index("{") + 1:body.index("}")]
# a member written "PageUp = Prior" is another name of the same key
alias = {}
for m in (m.strip() for m in body.split(",")):
    if m:
        name, _, of = (x.strip() for x in m.partition("="))
        alias[name] = alias.get(of, of) if of else name
over = open(gui + "/Chimera.Client.Common/input/DistinctKeyNameOverrides.cs").read()
renamed = dict(re.findall(r"DistinctKey\.(\w+)\s*=>\s*\"([^\"]+)\"", over))
# a key Chimera renames is known by that name alone, whichever of its names
# the rename is written with; another key by any of its names
renamed_keys = {alias[m]: n for m, n in renamed.items() if m in alias}
known = {renamed_keys.get(k, m) for m, k in alias.items()}
# Chimera merges the left and right modifier keys unless told not to
# (Config.MergeLAndRModifierKeys, true by default; Input.ModifierKeyPreMap):
# a press of either Shift arrives as "Shift". LeftShift and RightShift fire
# only with the merging off, so a panel that binds them must also bind the
# merged key, or Shift does nothing on a default setup.
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
