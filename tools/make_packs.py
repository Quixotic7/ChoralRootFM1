#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""Make the device manager's packs (docs/DEVICE-MANAGER.md): web/packs/factory-<engine>.json, one per engine, and
web/packs/index.json listing them.

  python3 tools/make_packs.py [JSON] [OUT_DIR]

JSON: the output of `./build/host/sound_templates --packs` (default: run that binary; build it with
`sh tests/run_cr_tests.sh` or the cc line at the top of tests/sound_templates.c). OUT_DIR: default web/packs.

A pack: {"format": "choralroot-pack", "version": 1, "name", "author", "licence", "engine", "sounds": [..]}, each
sound a choralroot-sound file (web/fm1sounds.js readSoundFile): the record SAVE would store for the factory preset
(bound to its own pool position) and the engine's patch where the engine has one (VA, FM6, CZ-1, FM TONE).
"""
import base64
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KIND = {13: "va", 12: "fm6", 14: "cz", 15: "quad"}
ORDER = [12, 15, 13, 14, 2, 3, 5, 6, 7, 9, 11]          # the picker's order (docs/DEVICE-MANAGER.md)


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def main(src=None, out=None):
    text = Path(src).read_text() if src else subprocess.run(
        [str(ROOT / "build/host/sound_templates"), "--packs"], check=True, capture_output=True, text=True).stdout
    data = json.loads(text)
    out = Path(out or ROOT / "web/packs")
    out.mkdir(parents=True, exist_ok=True)
    index = []
    engines = sorted(data, key=lambda e: ORDER.index(int(e)) if int(e) in ORDER else 99)
    for e in engines:
        v, eng = data[e], int(e)
        sounds = []
        for p in v["presets"]:
            rec = base64.b64decode(p["record"])
            if len(rec) != 192 or rec[0] != 0xA5 or rec[2] != eng:
                raise SystemExit(f"engine {e} preset {p['k']}: not a record of the engine")
            pos = p["k"] - v["first"] + 1
            sounds.append({"format": "choralroot-sound", "version": 1, "firmware": None, "created": None, "slot": None,
                           "name": p["name"], "engine": eng, "engineName": v["name"],
                           "binding": {"overwrites": pos, "name": p["name"]}, "record": p["record"],
                           "patch": {"kind": KIND[eng], "data": p["patch"]} if p["patch"] and eng in KIND else None})
        fname = f"factory-{slug(v['name'])}.json"
        pack = {"format": "choralroot-pack", "version": 1, "name": f"{v['name']} factory sounds",
                "author": "ChoralRoot FM-1 firmware (tools/make_packs.py)", "licence": "GPL-3.0-only",
                "engine": eng, "sounds": sounds}
        (out / fname).write_text(json.dumps(pack, indent=0) + "\n")
        index.append({"file": fname, "name": pack["name"], "engine": eng, "count": len(sounds),
                      "patches": sum(1 for s in sounds if s["patch"])})
    (out / "index.json").write_text(json.dumps({"format": "choralroot-pack-index", "version": 1, "packs": index}, indent=1) + "\n")
    print(f"packs: {out}: {len(index)} packs, {sum(p['count'] for p in index)} sounds "
          f"({sum(p['patches'] for p in index)} with a patch)")


if __name__ == "__main__":
    if len(sys.argv) > 3 or any(a in ("-h", "--help") for a in sys.argv[1:]):
        sys.exit(__doc__)
    main(*sys.argv[1:3])
