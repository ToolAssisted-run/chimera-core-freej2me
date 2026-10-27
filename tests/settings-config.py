#!/usr/bin/env python3
# Writes a copy of a Chimera config for a frontend run, with the given firmware
# remembered the way the Firmware window remembers it (keyed under this core's
# name). The run's sync settings are its project's, not the config's: a game
# runs only in a project (Chimera, 2026-10-26), and run-frontend.sh makes one
# with them. The settings argument stays for the callers' sake.
#
# Usage: settings-config.py <source config> <output config> <settings JSON> [<firmware JSON>]
# The optional firmware map is {"<decl id>": "<file path>"}.
import json
import sys

cfg = json.load(open(sys.argv[1]))
if len(sys.argv) > 4:
    fw = cfg.setdefault("CoreFirmware", {})
    for decl_id, path in json.loads(sys.argv[4]).items():
        fw["FreeJ2ME-Plus/" + decl_id] = path
json.dump(cfg, open(sys.argv[2], "w"), indent=2)
