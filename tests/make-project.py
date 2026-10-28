#!/usr/bin/env python3
"""Writes a .chimeraProject for a game, the way the New Project window
would: the files in their slots, the settings, the firmware and an input log
in the machine's own control names.

usage: make-project.py <package> <out.chimeraProject> <frames>
                       [<setting>=<value> ...] [press=<control>:<first>:<count> ...]
                       [firmware=<id>=<file> ...] [panel=<file>]
                       -- <slot=file> [<slot=file> ...]

A setting's value is written as JSON would have it: true, false, a number,
or text. firmware=<id>=<file> is a firmware file under the id the core
mounts it by (bios7.bin=<the BIOS file>).

A movie carries only the controls that are live, so the script must be told
which they are: panel=<file> is the output of the test harness's
--list-panel for the same game and settings.
"""
import hashlib
import json
import os
import re
import sys
import zipfile


def inputs_of(log):
    """A movie's text block ("[Input]", "LogKey:...", its entries, "[/Input]")
    as a project file's "inputs": the key's groups of column names, and one
    entry a frame. A group starts where a '#' begins a name; a name ends at
    '|' (as Chimera splits a key)."""
    lines = log.splitlines()
    out = {}
    key = next((l[len("LogKey:"):] for l in lines if l.startswith("LogKey:")), None)
    if key is not None:
        groups, at = [], 0
        while at < len(key):
            assert key[at] == "#", key
            groups.append([])
            at += 1
            while at < len(key) and key[at] != "#":
                bar = key.index("|", at)
                groups[-1].append(key[at:bar])
                at = bar + 1
        out["columns"] = groups
    out["entries"] = [l for l in lines if l.startswith("|")]
    return out


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 24), b""):
            h.update(chunk)
    return h.hexdigest().upper()


def holds(cond, settings):
    """the condition language of docs/project.md, the setting tests only"""
    if cond is None:
        return True
    if "all" in cond:
        return all(holds(c, settings) for c in cond["all"])
    if "any" in cond:
        return any(holds(c, settings) for c in cond["any"])
    if "not" in cond:
        return not holds(cond["not"], settings)
    if "setting" in cond:
        have = settings.get(cond["setting"])
        if "is" in cond:
            return have == cond["is"]
        return have in cond.get("in", [])
    return False


def player_of(name):
    return int(name[1]) if len(name) > 2 and name[0] == "P" and name[1].isdigit() else 0


def main():
    package, out, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])
    rest = sys.argv[4:]
    sep = rest.index("--")
    options, slotargs = rest[:sep], rest[sep + 1:]

    z = zipfile.ZipFile(package)
    cfg = json.loads(z.read("waterbox.config"))
    # a package of one machine says its controls and its system at the top;
    # a package of several has a "machine" setting and an entry for each
    if "machines" in cfg:
        ms = cfg.get("machineSetting", "machine")
        machine = next((o.split("=", 1)[1] for o in options if o.startswith(ms + "=")),
                       next(s["default"] for s in cfg["settings"] if s["name"] == ms))
        entry = next(m for m in cfg["machines"] if machine in m["when"])
    else:
        machine = None
        entry = {"id": cfg["systemId"], "input": cfg["input"]}
    buttons = entry["input"]["buttons"]
    axes = entry["input"].get("axes", [])

    presses = {}
    overrides = []
    given = []
    for o in options:
        k, v = o.split("=", 1)
        if k == "firmware":
            fid, path = v.split("=", 1)
            given.append({"id": fid, "sha1": sha1(path)})
        elif k == "panel":
            live, live_axes = set(), set()
            for line in open(v):
                word = line.split()
                if len(word) >= 3 and word[0] == "panel" and word[-1] == "active":
                    live.add(line[line.index("'") + 1:line.rindex("'")])
                elif len(word) == 3 and word[0] == "axis" and word[2] == "active":
                    live_axes.add(int(word[1]))
            buttons = [b for b in buttons if b in live]
            axes = [a for i, a in enumerate(axes) if i in live_axes]
        elif k == "press":
            control, first, count = v.rsplit(":", 2)
            presses.setdefault(control, []).append((int(first), int(count)))
        else:
            try:
                overrides.append((k, json.loads(v)))
            except ValueError:
                overrides.append((k, v))

    groups = max([player_of(a["name"]) for a in axes] + [player_of(b) for b in buttons] + [0]) + 1
    key = ""
    for g in range(groups):
        key += "#"
        for a in axes:
            if player_of(a["name"]) == g:
                key += a["name"] + "|"
        for b in buttons:
            if player_of(b) == g:
                key += b + "|"
    rows = []
    for f in range(frames):
        row = ""
        for g in range(groups):
            row += "|"
            for a in axes:
                if player_of(a["name"]) == g:
                    row += "%5d," % a.get("neutral", 0)
            for b in buttons:
                if player_of(b) == g:
                    row += "X" if any(first <= f < first + count for first, count in presses.get(b, [])) else "."
        rows.append(row + "|")
    log = "[Input]\nLogKey:" + key + "\n" + "\n".join(rows) + "\n[/Input]\n"

    files = []
    for arg in slotargs:
        slot, path = arg.split("=", 1)
        files.append({"name": os.path.basename(path), "sha1": sha1(path), "slot": slot})
        if path.lower().endswith(".cue"):
            # the track files a cue names come with it
            for line in open(path, errors="replace"):
                m = re.match(r'\s*FILE\s+"(.+)"', line)
                if m:
                    track = os.path.join(os.path.dirname(path), m.group(1))
                    files.append({"name": m.group(1), "sha1": sha1(track), "slot": "support"})

    settings = {cfg.get("machineSetting", "machine"): machine} if machine else {}
    for d in cfg.get("settings", []):
        if d["name"] not in settings and d.get("default") is not None and machine in d.get("when", [machine]):
            settings[d["name"]] = d["default"]
    for k, v in overrides:
        settings[k] = v

    # a file that was given stands for the package's entry of the same id: the
    # project records the hash of the file it was made with
    named = {g["id"] for g in given}
    firmware = [{"id": d["id"], "sha1": d.get("sha1", "")}
                for d in cfg.get("firmware", [])
                if d["id"] not in named and holds(d.get("requiredWhen"), settings)] + given

    project = {
        "id": "freej2me-test-%s" % os.path.splitext(files[0]["name"])[0],
        "title": "%s through Chimera" % files[0]["name"],
        "description": "written by tests/make-project.py",
        "core": {"name": cfg["coreName"], "version": cfg["version"], "sha1": sha1(package)},
        "rerecords": 0,
        "files": files,
        "settings": settings,
        "firmware": firmware,
        "coreCache": [],
        "inputs": inputs_of(log),
        "markers": [],
        "branches": [],
        "headers": {
            "MovieVersion": "Chimera Project File v1.1",
            "Platform": entry["id"],
            "SHA1": files[0]["sha1"] if files else "",
            "LastInputFrame": str(frames - 1),
            "VsyncNumerator": str(cfg["video"]["vsyncNumerator"]),
            "VsyncDenominator": str(cfg["video"]["vsyncDenominator"]),
        },
    }
    with open(out, "w") as f:
        json.dump(project, f, indent="\t")
    return 0


if __name__ == "__main__":
    sys.exit(main())
