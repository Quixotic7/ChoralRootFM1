#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""PPM -> PNG for the UI renders of tests/ui_render.c (Pillow). Under OUTDIR (default build/ui_new):
  (the STEP page's piano roll scenes, roll_* step chance drum: also in build/ui_roll/<PALETTE>_<screen>.png)
  <PALETTE>/<screen>.png   240 x 240, true size
  sheet_<PALETTE>.png      every screen of one palette, 1x, with its name
With SLOTDIR (ui_render.py OUTDIR SLOTDIR): its filmstrips of the rolling digits, SLOTDIR/*.ppm -> *.png.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/ui_new")
shots = {}
for f in sorted((out / "ppm").glob("*.ppm")):
    pal, name = f.stem.split("_", 1)
    (out / pal).mkdir(parents=True, exist_ok=True)
    img = Image.open(f).convert("RGB")
    img.save(out / pal / f"{name}.png")
    shots.setdefault(pal, []).append((name, img))
    if pal in ("MONO", "GREEN") and (name.startswith("perform_") or name == "menu_hold"):   # the FX layer's screens
        (out.parent / "ui_fx").mkdir(parents=True, exist_ok=True)
        img.save(out.parent / "ui_fx" / f"{pal}_{name}.png")
    if pal in ("MONO", "GREEN") and (name.startswith("layer_") or name.startswith("perform_")):   # the quick layers
        (out.parent / "ui_layers").mkdir(parents=True, exist_ok=True)
        img.save(out.parent / "ui_layers" / f"{pal}_{name}.png")
    if name.startswith("roll_") or name in ("step", "chance", "drum"):   # the STEP page's piano roll (and the grid)
        (out.parent / "ui_roll").mkdir(parents=True, exist_ok=True)
        img.save(out.parent / "ui_roll" / f"{pal}_{name}.png")
    if pal in ("MONO", "GREEN") and (name.startswith("name_") or name in ("project_named", "song_named", "user_foot")):
        (out.parent / "ui_name").mkdir(parents=True, exist_ok=True)   # NAME (src/ui_name.c) and the names it shows
        img.save(out.parent / "ui_name" / f"{pal}_{name}.png")
for pal in ("MONO", "GREEN"):   # DIGITAL's 8 algorithm charts (+ OP LEVEL): x3, the panel, in build/ui_alg/
    algs = [(n, im) for n, im in shots.get(pal, []) if n.startswith("alg_") or n == "op_level"]
    if not algs:
        continue
    (out.parent / "ui_alg").mkdir(parents=True, exist_ok=True)
    pw, ph, lh = 240 * 3, 122 * 3, 18
    sheet = Image.new("RGB", (2 * pw + 24, ((len(algs) + 1) // 2) * (ph + lh + 8) + 8), (90, 90, 90))
    d = ImageDraw.Draw(sheet)
    for i, (name, img) in enumerate(algs):
        big = img.resize((720, 720), Image.NEAREST)
        big.save(out.parent / "ui_alg" / f"{pal}_{name}.png")
        x, y = 8 + (i % 2) * (pw + 8), 8 + (i // 2) * (ph + lh + 8)
        sheet.paste(big.crop((0, 76 * 3, pw, 198 * 3)), (x, y))
        d.text((x, y + ph + 3), name.replace("alg_", "ALG "), fill=(235, 235, 235))
    sheet.save(out.parent / "ui_alg" / f"sheet_{pal}.png")
for pal, items in shots.items():
    cols, cw, ch = 8, 248, 262
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cw + 8, rows * ch + 8), (90, 90, 90))
    d = ImageDraw.Draw(sheet)
    for i, (name, img) in enumerate(items):
        x, y = 8 + (i % cols) * cw, 8 + (i // cols) * ch
        sheet.paste(img, (x, y))
        d.text((x, y + 242), name, fill=(235, 235, 235))
    sheet.save(out / f"sheet_{pal}.png")
print(f"ui_render: PNGs in {out}/<palette>/ and {out}/sheet_<palette>.png")
if len(sys.argv) > 2:
    for f in sorted(Path(sys.argv[2]).glob("*.ppm")):
        Image.open(f).convert("RGB").save(f.with_suffix(".png"))
    print(f"ui_render: rolling-digit filmstrips in {sys.argv[2]}/*.png")
