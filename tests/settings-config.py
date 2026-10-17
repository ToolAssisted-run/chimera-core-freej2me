#!/usr/bin/env python3
"""Writes a copy of a Chimera configuration with this core's settings in it.

  settings-config.py <configuration> <output> <settings as JSON> [<firmware as JSON>]

The settings are stored the way Chimera's settings window stores them: a map
of name to value under the adapter's type name. The firmware is a map of a
firmware id to a file, stored the way the Firmware
window stores it, under this core's name. The core is also made the one
Chimera picks for J2ME, because the Cores folder may hold
another.
"""
import json
import sys

cfg = json.load(open(sys.argv[1]))
cfg.setdefault("CoreSettings", {})["Chimera.Emulation.Common.Waterbox.WaterboxCore"] = {
    "Values": json.loads(sys.argv[3])
}
firmware = cfg.setdefault("CoreFirmware", {})
for firmware_id, path in json.loads(sys.argv[4] if len(sys.argv) > 4 else "{}").items():
    firmware["FreeJ2ME-Plus/" + firmware_id] = path
for system in ("J2ME",):
    cfg.setdefault("DefaultCores", {})[system] = "FreeJ2ME-Plus"
json.dump(cfg, open(sys.argv[2], "w"), indent=2)
