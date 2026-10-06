# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Shared rasteriser of the UI fonts and icons (gen_aa_font.py, gen_aa_icons.py).

TTF -> trimmed 4-bit alpha bitmaps. Pillow + FreeType (+ Raqm for kerning and OpenType features);
fontTools is not needed here. Nothing is downloaded, nothing is installed.

Bitmap format (shared by glyphs and icons)
  alpha 0..15, row-major, 2 pixels per byte (high nibble first), rows packed back to back
  (no padding between rows), every bitmap starts on a byte boundary.
  A glyph is trimmed to its ink box (bw x bh); an icon is a square cell (no trimming).

Glyphs are drawn at 16 x the size and box-filtered down (unhinted area coverage), once per horizontal phase:
FreeType's grid fitting at the small size snapped each glyph's stems and bowls to whole pixels on its own and
moved it up to 0.8 px off its fractional advance, which made the space between round and straight letters
uneven (text_spacing_test.py measures it).
"""
import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def open_font(spec, px):
    """spec = "path[#index][@wght]" (a .ttc takes the face index, a variable font the weight); '~' is expanded."""
    spec, _, wght = spec.partition("@")
    path, _, idx = spec.partition("#")
    path = os.path.expanduser(path)
    if not os.path.exists(path):
        raise SystemExit(f"font not found: {path}")
    f = ImageFont.truetype(path, px, index=int(idx) if idx else 0, layout_engine=ImageFont.Layout.RAQM)
    if wght:
        f.set_variation_by_axes([float(wght)])
    return f


def alpha4(v, gamma=1.0):
    """0..255 coverage -> 0..15; gamma < 1 makes thin light-on-dark text bolder."""
    return min(15, int(round(15.0 * ((v / 255.0) ** gamma))))


def pack(nibbles):
    """list of 0..15 -> bytes (high nibble first, odd count padded with 0)."""
    if len(nibbles) & 1:
        nibbles = nibbles + [0]
    return bytes((nibbles[i] << 4) | nibbles[i + 1] for i in range(0, len(nibbles), 2))


def render_cell(font, ch, size, ox, oy, features=None, anchor="ls"):
    """one character drawn at (ox, oy) on a size x size L image (anchor: baseline-left by default)."""
    img = Image.new("L", size, 0)
    ImageDraw.Draw(img).text((ox, oy), ch, font=font, fill=255, anchor=anchor, features=features)
    return img


SS = 16          # supersampling of the glyphs: drawn at 16 x the size, then box-filtered down


def _ss_cell(font_hi, ch, w, h, ox16, base, features=None):
    """one character at 16 x, its origin at ox16 / 16 px (a fraction is a horizontal phase) on a w x h px cell,
    box-filtered to w x h: area coverage of the unhinted outline (FreeType's grid fitting at 16 x is within
    1/32 px). The small-size rendering would snap stems and bowls to whole pixels one glyph at a time, which
    moves every glyph by up to +-0.8 px against its fractional advance (bowls the most)."""
    img = render_cell(font_hi, ch, (w * SS, h * SS), ox16, base * SS, features=features)
    return img.reduce(SS)


def _centroid_x(img):
    w, v = img.width, img.tobytes()
    cols = [sum(v[x::w]) for x in range(w)]
    m = sum(cols)
    return sum((x + 0.5) * c for x, c in enumerate(cols)) / m if m else None


def _phase(font_hi, ch, w, h, ox16, base, features, gamma):
    """the 4-bit glyph whose ink centroid lands nearest the true one for the origin ox16 / 16 px: the 4-bit
    rounding of the edge pixels moves a glyph by up to ~0.05 px, so the origin may move by up to 2 / 16 px
    to take that back (the 8-bit box filter is the reference)"""
    o = int(round(ox16))
    true = _centroid_x(_ss_cell(font_hi, ch, w, h, o, base, features))
    if true is None:
        return None
    true += (ox16 - o) / SS
    best = None
    for d in (0, -1, 1, -2, 2):
        nib = _ss_cell(font_hi, ch, w, h, o + d, base, features).point(lambda v: alpha4(v, gamma))
        c = _centroid_x(nib)
        if c is None:
            continue
        err = abs(c - true)
        if best is None or err < best[0] - 1e-9:
            best = (err, nib)
    return best and best[1]


FIXED = "0123456789."       # one phase, at the rounded pen: src/ui_draw.c roll_text draws a number a character at
                            # a time (a sign leads the number: at pen 0 it is phase 0 either way)
WHOLE = "0123456789."       # with tabular, their advance in whole pixels too: a number keeps an even pitch


def raster_font(spec, px, chars, tracking=0.0, gamma=1.0, tabular=True, kern_min=1, phases=1):
    """-> dict(h, asc, phases, glyphs{ch: (adv16, [(bx, by, bw, bh, bytes) per phase, or one])}, kern{(a,b): d16})

    Advances, kerning and the outlines come from the instanced weight at 16 x the size (unhinted): advances in
    1/16 px, so text width is exact over a string. phases: horizontal positions rasterised per glyph (1, 2, 4):
    phase p is the glyph drawn at a pen of +p/phases px; src/gfx.c cv_text takes one of the two phase positions
    around the pen, so a glyph sits less than a step (1/phases px) from its true place and neighbours keep their
    space within about half a step. The FIXED characters have one phase and sit at the rounded pen: a number
    drawn a character at a time at its rounded pens (the rolling digits) then lands exactly where the whole
    string does; with tabular, the WHOLE ones also advance by whole pixels, so a number has no rounding at all.
    tracking: extra advance per glyph in em (negative = tighter; the stand-in for "Inter Tight").
    tabular: digits share one advance (the widest, OpenType tnum), each centred in it, so values do not jiggle.
    kern_min: keep kerning pairs of at least this many 1/16 px.
    """
    assert phases in (1, 2, 4, 8)
    font = open_font(spec, px)                 # the line metrics only (ascent / descent rounded to pixels)
    font_hi = open_font(spec, px * SS)
    asc, desc = font.getmetrics()
    pad = px                                   # canvas margin around the glyph (overhang, accents)
    cw, chh = px * 3, asc + desc + 2 * pad
    base = pad + asc                           # baseline row of the canvas
    feats = ["tnum"] if tabular else None
    glyphs, adv, nat = {}, {}, {}
    for ch in chars:
        f = feats if ch.isdigit() else None
        nat[ch] = adv[ch] = font_hi.getlength(ch, features=f) / SS + tracking * px
    if tabular:
        digits = [c for c in chars if c.isdigit()]
        if digits:
            wide = max(adv[c] for c in digits)
            for c in digits:
                adv[c] = wide
        for c in chars:
            if c in WHOLE:
                adv[c] = float(max(1, round(adv[c])))
    bot_max = 0
    for ch in chars:
        a16 = int(round(adv[ch] * 16))
        f = feats if ch.isdigit() else None
        shift = (adv[ch] - nat[ch]) / 2 if tabular and ch in WHOLE else 0.0   # centre the figure in the cell
        ph = []
        for p in range(1 if ch in FIXED else phases):
            nib = None if ch == " " else _phase(font_hi, ch, cw, chh, (pad + shift + p / phases) * SS, base, f, gamma)
            bb = nib and nib.getbbox()
            if not bb:
                ph.append((0, 0, 0, 0, b""))
                continue
            x0, y0, x1, y1 = bb
            ph.append((x0 - pad, y0 - pad, x1 - x0, y1 - y0, pack(list(nib.crop(bb).tobytes()))))
            bot_max = max(bot_max, y1 - pad)
        glyphs[ch] = (a16, ph)
    h = max(asc + desc, bot_max)
    kern = {}
    lens = {c: font_hi.getlength(c, features=feats) for c in chars if c != " "}
    for a in chars:
        for b in chars:
            if a == " " or b == " ":
                continue
            if a.isdigit() and b.isdigit():
                continue                       # tabular figures never kern
            d = (font_hi.getlength(a + b, features=feats) - lens[a] - lens[b]) / SS
            d16 = int(round(d * 16))
            if abs(d16) >= kern_min:
                kern[(a, b)] = max(-127, min(127, d16))
    return {"h": h, "asc": asc, "glyphs": glyphs, "kern": kern, "px": px, "phases": phases}


def raster_icon(spec, px, codepoint, gamma=1.0):
    """one icon glyph in a px x px cell (icon fonts are drawn on a square em) -> bytes (4-bit alpha)."""
    font = open_font(spec, px * SS)            # 16 x and box-filtered: hinting at px squashed flat glyphs
    img = render_cell(font, chr(codepoint), (px * SS, px * SS), 0, 0, anchor="la").reduce(SS)
    return pack([alpha4(v, gamma) for v in img.tobytes()]), img.getbbox() is not None


def c_array(name, ctype, values, per_line=16, fmt="{}"):
    lines = [f"static const {ctype} {name}[{len(values)}] = {{"]
    for i in range(0, len(values), per_line):
        lines.append("    " + ", ".join(fmt.format(v) for v in values[i:i + per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)
