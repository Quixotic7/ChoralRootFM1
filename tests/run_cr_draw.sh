#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
# The ChoralRoot screen renderer on the host (firmware/src/cr_draw.c, tests/cr_draw_test.c). From the repo root:
#   sh tests/run_cr_draw.sh
# 1. build/gen/*.h if missing (tools/build.py generate(): the cr faces are build/gen/cr_fonts.h, MOD is in ui_palettes.h)
# 2. tests/cr_screens_gen.h from design/choralroot-fm1-mockups.json (tests/gen_cr_screens.py)
# 3. renders every state settled and mid-animation, the layout lint and the cache / animation checks
#    (build/cr_screens/report.txt)
# 4. PNGs (Pillow): build/cr_screens/<nn>_<slug>.png and <nn>_<slug>_mid.png (240 x 240, true size),
#    build/cr_screens/sheet.png laid out as design/choralroot-fm1-screens.png (2x, nearest), and
#    build/cr_screens/compare.png: each state's mock-up (from that sheet) beside the device render.
set -e
cd "$(dirname "$0")/.."
export DYLD_FALLBACK_LIBRARY_PATH="${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib}"
if [ ! -f build/gen/cr_fonts.h ] || [ ! -f build/gen/ui_palettes.h ] || ! grep -q '"MOD"' build/gen/ui_palettes.h; then
    python3 -c 'import sys; sys.path.insert(0, "tools"); import build; build.generate()'
fi
python3 tests/gen_cr_screens.py
OUT=build/cr_screens
mkdir -p "$OUT/ppm" build/host
${CC:-cc} -std=c99 -O1 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare \
    -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_draw_test tests/cr_draw_test.c
status=0
build/host/cr_draw_test "$OUT" || status=$?
python3 - "$OUT" <<'EOF'
import json, sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

out = Path(sys.argv[1])
design = json.loads(Path("design/choralroot-fm1-mockups.json").read_text())
names = [s.get("name", "") for s in design["states"]]
shots = []
for f in sorted((out / "ppm").glob("*.ppm")):
    img = Image.open(f).convert("RGB")
    img.save(out / f"{f.stem}.png")
    if not f.stem.endswith("_mid"):
        shots.append((f.stem, img))

def font(px, bold=False):
    for p in ("/System/Library/Fonts/Supplemental/Arial Bold.ttf" if bold else "/System/Library/Fonts/Supplemental/Arial.ttf",
              "/Library/Fonts/Arial.ttf"):
        try:
            return ImageFont.truetype(p, px)
        except OSError:
            pass
    return ImageFont.load_default()

def rounded_mask(sw, r):
    m = Image.new("L", (sw, sw), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, sw - 1, sw - 1), r, fill=255)
    return m

# the sheet: the designer's renderScreensSheet (index.html): S 2, GAP 28, 4 columns, the name under each
S, GAP, NAME_H = 2, 28, 30
SW, COLS = 240 * S, 4
rows = (len(shots) + COLS - 1) // COLS
W, H = GAP + COLS * (SW + GAP), GAP + 36 + rows * (SW + NAME_H + GAP)
sheet = Image.new("RGB", (W, H), (0x16, 0x16, 0x1a))
d = ImageDraw.Draw(sheet)
d.text((GAP, GAP - 8), "ChoralRoot FM-1 mockups (device render, cr_draw.c)", fill=(0xd8, 0xd8, 0xdc), font=font(22, True))
d.text((W - GAP, GAP - 4), "FM-1 · 240×240 screens", fill=(0x8a, 0x8a, 0x92), font=font(14), anchor="ra")
mask = rounded_mask(SW, 10)
for n, (stem, img) in enumerate(shots):
    x, y = GAP + (n % COLS) * (SW + GAP), GAP + 36 + (n // COLS) * (SW + NAME_H + GAP)
    sheet.paste(img.resize((SW, SW), Image.NEAREST), (x, y), mask)
    d.rounded_rectangle((x, y, x + SW - 1, y + SW - 1), 10, outline=(0, 0, 0), width=2)
    label = names[n] if n < len(names) else stem
    d.text((x + SW // 2, y + SW + 20), label, fill=(0xd8, 0xd8, 0xdc), font=font(14, True), anchor="ms")
sheet.save(out / "sheet.png")

# compare: the mock-up cell (cut from design/choralroot-fm1-screens.png, the same layout) | the device | mid-animation
ref = Path("design/choralroot-fm1-screens.png")
if ref.exists():
    mock = Image.open(ref).convert("RGB")
    cw = 3 * SW + 4 * 12
    comp = Image.new("RGB", (2 * cw, ((len(shots) + 1) // 2) * (SW + 40) + 12), (0x16, 0x16, 0x1a))
    dc = ImageDraw.Draw(comp)
    for n, (stem, img) in enumerate(shots):
        x, y = (n % 2) * cw + 12, (n // 2) * (SW + 40) + 12
        mx, my = GAP + (n % COLS) * (SW + GAP), GAP + 36 + (n // COLS) * (SW + NAME_H + GAP)
        comp.paste(mock.crop((mx, my, mx + SW, my + SW)), (x, y))
        comp.paste(img.resize((SW, SW), Image.NEAREST), (x + SW + 12, y))
        mid = out / f"{stem}_mid.png"
        if mid.exists():
            comp.paste(Image.open(mid).convert("RGB").resize((SW, SW), Image.NEAREST), (x + 2 * SW + 24, y))
        dc.text((x, y + SW + 18), f"{names[n] if n < len(names) else stem}   (mock-up | device | mid-animation)",
                fill=(0xd8, 0xd8, 0xdc), font=font(14, True))
    comp.save(out / "compare.png")
print(f"cr_draw: PNGs in {out}/, the contact sheet {out}/sheet.png, side by side with the mock-ups {out}/compare.png")
EOF
exit $status
