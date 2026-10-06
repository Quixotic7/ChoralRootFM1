#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""The UI palettes (run by tools/build.py generate()).

  gen_ui_palettes.py OUT.h [--report]

A palette is five colours: BG (background), SURF (surface: dialogs, menu rows, mixer strips),
TEXT, THEME (identity colour: values, curves, gauges, selection) and ACCENT (the one active
thing: hot knob, cursor, playhead, a sounding step). The firmware derives the rest with fixed
blends at palette_set() time (src/gfx.c, the same integer maths as mix() here):
  MID  = mix(BG, TEXT, 70 %)    labels, units, secondary text
  DIM  = mix(BG, TEXT, 42 %)    inactive, empty steps, disabled
  LINE = mix(BG, TEXT, 18 %)    1 px dividers, the faint gauge track
  SEL  = mix(BG, THEME, 80 %)   selection fill and gauge fill (the "mid" of the theme)
  TINT = mix(BG, THEME, 12 %)   a faint area (the selected drum lane)
  RAISE = mix(SURF, TEXT, 12 %) a raised area on a surface: empty step stubs, guides, the selected drum
                                lane, fader slots, button wells, chips
  KEY  = mix(BG, TEXT, 78 %)    a keycap's fill (a mid-light cushion; its label INK, unavailable: a DIM fill)
  INK  = BG: text on a selection or THEME fill (MONO: a light fill, dark ink)
Outside the palette: REC, a fixed red (MONO: ACCENT); the QR code's fixed black and white; the crash
screen's fixed red. MONO is pure grayscale (R = G = B) in every colour and every derived tint.
Saved settings name palettes by a tagged id (PAL_TAG + index); older firmware saved the index into
its 20 palettes, mapped by UI_PALETTE_MIGRATE.
--report prints the WCAG contrast of every pairing. The tool fails (exit 1) on any miss.
"""
import argparse
import sys
from pathlib import Path

# name, bg, surf, text, theme, accent (8-bit RGB; stored as RGB565)
PALETTES = [
    ("MONO",   (14, 14, 14),    (36, 36, 36),    (204, 204, 204), (232, 232, 232), (255, 255, 255)),
    ("GREEN",  (6, 18, 10),     (16, 40, 26),    (226, 244, 230), (84, 214, 120),  (255, 214, 92)),
    ("AMBER",  (18, 14, 8),     (42, 32, 18),    (246, 236, 216), (255, 168, 40),  (96, 214, 230)),
    ("ICE",    (8, 16, 24),     (20, 36, 52),    (228, 240, 248), (80, 184, 236),  (255, 140, 100)),
    ("VIOLET", (16, 12, 28),    (36, 28, 60),    (240, 232, 252), (178, 136, 246), (110, 228, 168)),
    ("ROSE",   (24, 10, 18),    (52, 22, 40),    (252, 232, 242), (244, 114, 182), (255, 214, 110)),
    ("PAPER",  (244, 239, 228), (226, 218, 202), (34, 30, 24),    (10, 84, 70),    (172, 56, 8)),
    ("HI-CON", (0, 0, 0),       (40, 40, 40),    (255, 255, 255), (255, 232, 0),   (0, 230, 255)),
    # ChoralRoot FM-1's mod palette (PLAN.md section 5): white on black, its red as THEME, its yellow as ACCENT. The
    # red asked for, (224, 58, 47), misses ink/sel (3.44 < 4.5: BG text on a SEL row) and theme/surf (4.05 < 4.5);
    # THEME is the nearest red that passes every check. ChoralRoot's screens draw the named red itself
    # (firmware/src/cr_draw.c CR_RED = (224, 58, 47)); THEME here serves Felucca's pages in this palette.
    ("MOD",    (10, 10, 12),    (28, 28, 32),    (246, 243, 234), (248, 96, 48),   (242, 183, 5)),
]
# the 20 palettes of the earlier firmware (index order) -> the new palette
OLD = ["GREEN", "AMBER", "CYAN", "RED", "MONO", "VIOLET", "PINK", "ICE", "WARM", "OCEAN", "DUSK", "HI-CON",
       "LIGHT", "PAPER", "SKY", "MINT", "LILAC", "ROSE", "SAND", "L-HICON"]
OLD_TO_NEW = {"GREEN": "GREEN", "AMBER": "AMBER", "CYAN": "ICE", "RED": "ROSE", "MONO": "MONO", "VIOLET": "VIOLET",
              "PINK": "ROSE", "ICE": "ICE", "WARM": "AMBER", "OCEAN": "ICE", "DUSK": "VIOLET", "HI-CON": "HI-CON",
              "LIGHT": "PAPER", "PAPER": "PAPER", "SKY": "PAPER", "MINT": "PAPER", "LILAC": "PAPER", "ROSE": "PAPER",
              "SAND": "PAPER", "L-HICON": "HI-CON"}
PAL_TAG = 64                       # stored id = PAL_TAG + index; below 20: an old id
REC_DARK, REC_LIGHT = (255, 72, 72), (190, 24, 40)
QR_LIGHT, QR_DARK, CRASH_BG, CRASH_INK = (255, 255, 255), (0, 0, 0), (160, 0, 0), (255, 255, 255)
PCT = {"MID": 70, "DIM": 42, "LINE": 18, "SEL": 80, "TINT": 12, "KEY": 78}
RAISE_PCT = 12                     # SURF -> TEXT


def to565(c):
    r, g, b = c
    if r == g == b:                     # a gray stays on the RGB565 gray axis (G = 2 R)
        return ((r >> 3) << 11) | ((r >> 3) << 6) | (r >> 3)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def from565(v):
    return ((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)


def cdiv(a, b):
    return a // b if a >= 0 else -((-a) // b)


def mix(a, b, pct, mono=False):
    """src/gfx.c ux_mix: a + (b - a) * pct / 100 per channel, rounded; MONO on the 5-bit red channel"""
    if mono:
        x, y = a >> 11, b >> 11
        d = (y - x) * pct
        v = x + cdiv(d + (50 if d >= 0 else -50), 100)
        return (v << 11) | (v << 6) | v
    out = 0
    for sh, mask in ((11, 31), (5, 63), (0, 31)):
        x, y = (a >> sh) & mask, (b >> sh) & mask
        d = (y - x) * pct
        out |= (x + cdiv(d + (50 if d >= 0 else -50), 100)) << sh
    return out


def luma(c):
    """src/gfx.c ux_luma: 0..255, integer"""
    r, g, b = c >> 11, (c >> 5) & 63, c & 31
    return (r * 255 // 31 * 54 + g * 255 // 63 * 183 + b * 255 // 31 * 19) >> 8


def lum(c565):
    def lin(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = from565(c565)
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def contrast(a, b):
    la, lb = lum(a), lum(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def derive(p):
    name, *cols = p
    bg, surf, text, theme, accent = map(to565, cols)
    mono = name == "MONO"
    d = dict(bg=bg, surf=surf, text=text, theme=theme, accent=accent,
             mid=mix(bg, text, PCT["MID"], mono), dim=mix(bg, text, PCT["DIM"], mono),
             line=mix(bg, text, PCT["LINE"], mono), sel=mix(bg, theme, PCT["SEL"], mono),
             tint=mix(bg, theme, PCT["TINT"], mono), raise_=mix(surf, text, RAISE_PCT, mono),
             key=mix(bg, text, PCT["KEY"], mono))
    d["ink"] = bg
    d["light"] = luma(bg) > 128
    d["rec"] = accent if mono else to565(REC_LIGHT if d["light"] else REC_DARK)
    return name, d


# (foreground, background, minimum contrast, why)
CHECKS = [("text", "bg", 12.0, "body text"), ("text", "surf", 9.0, "body text on a surface"),
          ("mid", "bg", 5.0, "labels"), ("mid", "surf", 4.0, "labels on a surface"),
          ("dim", "bg", 2.2, "inactive (not read)"), ("line", "bg", 1.25, "1 px dividers"),
          ("theme", "bg", 4.5, "values, curves"), ("theme", "surf", 4.5, "values on a surface"),
          ("accent", "bg", 4.5, "hot value, cursor, playhead"), ("accent", "surf", 4.5, "hot on a surface"),
          ("ink", "sel", 4.5, "text on a selection"), ("ink", "theme", 4.5, "text on a THEME fill"),
          ("sel", "bg", 1.8, "gauge fill vs track"), ("rec", "bg", 3.0, "REC (graphic)"),
          ("text", "raise_", 7.0, "text on a RAISE chip or button"), ("ink", "rec", 4.5, "text on the REC chip"),
          ("ink", "key", 4.5, "a keycap's label"), ("key", "bg", 3.0, "a keycap on the background"),
          ("key", "surf", 2.5, "a keycap on a surface"), ("ink", "dim", 1.8, "an unavailable keycap's label"),
          ("ink", "accent", 4.5, "text on the ARM badge")]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", nargs="?")
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    fails = []
    assert PALETTES[0][0] == "MONO" and len(OLD) == 20 and len(PALETTES) <= 63
    rows = [derive(p) for p in PALETTES]
    for name, d in rows:
        if name == "MONO":
            for k, v in d.items():
                if k == "light":
                    continue
                if not (v >> 11 == v & 31 and ((v >> 5) & 63) == (v >> 11) << 1):
                    fails.append(f"MONO is not grayscale: {k} = {v:#06x}")
        for f, b, lim, why in CHECKS:
            c = contrast(d[f], d[b])
            if c < lim:
                fails.append(f"{name}: {f}/{b} {c:.2f} < {lim} ({why})")
    if a.report:
        print(f"{'palette':8s} " + " ".join(f"{f}/{b}".ljust(12) for f, b, _, _ in CHECKS))
        for name, d in rows:
            print(f"{name:8s} " + " ".join(f"{contrast(d[f], d[b]):5.1f}".ljust(12) for f, b, _, _ in CHECKS))
        print("minimum  " + " ".join(f"{lim:<12.2f}" for _, _, lim, _ in CHECKS))
    if a.out:
        names = [p[0] for p in PALETTES]
        out = ["/* generated by tools/gen_ui_palettes.py: the UI palettes (5 colours each, the rest derived) */",
               "#pragma once", "#include <stdint.h>", "",
               "/* ui_pal_t {name, bg, surf, text, theme, accent} (src/gfx.c) */",
               "static const ui_pal_t UI_PALETTES[] = {"]
        for p in PALETTES:
            v = [to565(c) for c in p[1:]]
            out.append(f'    {{"{p[0]}", ' + ", ".join(f"0x{x:04x}" for x in v) + "},")
        out += ["};", f"#define UI_NPALETTES {len(PALETTES)}u", "#define UI_MONO_INDEX 0u",
                f"#define UI_PAL_TAG {PAL_TAG}u          /* stored id = UI_PAL_TAG + index; below 20: an old id */"]
        out += [f"#define UI_{k}_PCT {v}" for k, v in PCT.items()] + [f"#define UI_RAISE_PCT {RAISE_PCT}   /* SURF -> TEXT */"]
        out += [f"#define UI_REC_DARK 0x{to565(REC_DARK):04x}u", f"#define UI_REC_LIGHT 0x{to565(REC_LIGHT):04x}u",
                f"#define UI_QR_LIGHT 0x{to565(QR_LIGHT):04x}u", f"#define UI_QR_DARK 0x{to565(QR_DARK):04x}u",
                f"#define UI_CRASH_BG 0x{to565(CRASH_BG):04x}u", f"#define UI_CRASH_INK 0x{to565(CRASH_INK):04x}u", "",
                "/* the 20 palettes of earlier firmware: " + " ".join(OLD) + " */",
                "static const uint8_t UI_PALETTE_MIGRATE[20] = {" +
                ", ".join(str(names.index(OLD_TO_NEW[n])) for n in OLD) + "};", ""]
        Path(a.out).write_text("\n".join(out))
    for f in fails:
        print("FAIL", f)
    if fails:
        sys.exit(1)


if __name__ == "__main__":
    main()
