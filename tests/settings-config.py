#!/usr/bin/env python3
# Writes the window's settings for a frontend run: empty but for the firmware
# given, remembered as the Firmware window remembers a file chosen by hand
# (keyed under this core's name). The run's sync settings are its project's,
# not the settings file's: a game runs only in a project (Chimera,
# 2026-10-26), and run-frontend.sh makes one with them. The settings argument
# stays for the callers' sake.
#
# Usage: settings-config.py <output settings> <settings JSON> [<firmware JSON>]
# The optional firmware map is {"<decl id>": "<file path>"}.
import json
import sys

out = {}
if len(sys.argv) > 3:
    # a setting that holds JSON is kept as JSON text
    out["cores.chosenFirmware"] = json.dumps({"FreeJ2ME-Plus": json.loads(sys.argv[3])})
json.dump(out, open(sys.argv[1], "w"), indent=2)
