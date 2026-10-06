#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Text spacing against the font itself: the generated glyph tables (build/gen/ui_fonts.h), placed as
src/gfx.c cv_text places them (tests/text_ref_test.c pins cv_text to that rule), against a 16 x supersampled,
unhinted rendering of the same instanced weight, every glyph at its exact fractional pen from shaping the
whole string (advances and GPOS kerning at 16 x the size).

Per glyph: its displacement d = ink centroid (tables) - ink centroid (reference); area coverage keeps the first
moment, so d is how far the glyph sits from its true place. Per pair: gap error = d[i+1] - d[i], the error of
the space between two neighbours. Fails when a pair of S or M is off by more than 0.25 px or L by more than 0.5 px.

  text_spacing_test.py [ui_fonts.h] [--png DIR [--tag NAME]] [--rule gfx|rounded] [--no-limit]
  (PNG: the specimen strings x4, gray on black; --rule rounded measures tables made before the phases)
"""
import argparse
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))
import aa_raster as ar  # noqa: E402
import gen_aa_font as gf  # noqa: E402

SS = 16
LOWER = ["oeo", "coco", "eco", "nono", "minimum", "hello", "level", "decay", "release", "sound", "Pitch", "Cutoff",
         "doob", "bdpq", "occasion", "accessed", "Resonance", "Envelope", "Attack", "Sustain", "Volume", "Tempo",
         "Swing", "Pattern", "Chance", "Multi-engine synthesizer", "H\xfcgelton Instruments", "Hello world",
         "Mixed case text", "Glide", "Drive", "Feedback", "Motion", "Phrases", "Song", "Mixer", "Calibration"]
UPPER = ["CUTOFF", "RESO", "LEVEL", "DECAY", "RELEASE", "SOUND", "OCOE", "COCO", "NONO", "MINIMUM", "SUPER SAW",
         "FELUCCA", "PRESETS", "ANALOG", "DIGITAL", "PITCH", "SWING", "AV AW LT TA YO"]
RULE = ["gfx"]   # "gfx": src/gfx.c cv_text; "rounded": the earlier rule (each glyph at the rounded pen, 1 phase)
LIMIT = {"S": 0.25, "M": 0.25, "L": 0.5}


def parse(path):
    """the faces of a generated ui_fonts.h -> {name: dict(h, asc, first, last, ex, psh, g, data, kern)}"""
    t = Path(path).read_text()
    faces = {}
    for m in re.finditer(r"static const aafont_t AF_(\w+) = \{([^}]*)\};", t):
        name, f = m.group(1), [x.strip() for x in m.group(2).split(",")]
        arr = lambda n: [int(v, 0) for v in re.search(r"AF_%s_%s\[\d+\] = \{([^}]*)\}" % (name, n), t)
                         .group(1).replace("\n", " ").split(",") if v.strip()]
        g = [tuple(int(v) for v in e.split(",")) for e in
             re.findall(r"\{(-?\d+, -?\d+, -?\d+, -?\d+, -?\d+, -?\d+)\}",
                        re.search(r"AF_%s_G\[\d+\] = \{(.*?)\n\};" % name, t, re.S).group(1))]
        kern = {}
        if f[8] != "0":
            kern = dict(zip(arr("KERN"), arr("KD")))
        faces[name] = {"h": int(f[0]), "asc": int(f[1]), "first": int(f[2]), "last": int(f[3]), "nex": int(f[4]),
                       "g": g, "data": bytes(arr("DATA")), "kern": kern, "ex": arr("EX")[:int(f[4])],
                       "psh": int(f[11]) if len(f) > 11 else 0,
                       "hc": arr("HC") if len(f) > 12 and f[12] != "0" else None}
    return faces


def glyph_at(f, c):
    """the glyph number of code c in the face, None = not in it"""
    if f["first"] <= c <= f["last"]:
        return c - f["first"]
    if c in f["ex"]:
        return f["last"] - f["first"] + 1 + f["ex"].index(c)
    return None


def glyph(f, c):
    """src/gfx.c glyph(): the index of the glyph (its phase 0 entry is g[index << psh])"""
    if ord("a") <= c <= ord("z") and f["last"] < ord("a"):
        c -= 32
    k = glyph_at(f, c)
    if k is None and c == 0x85:
        k = glyph_at(f, ord("."))
    if k is None:
        k = glyph_at(f, ord("?"))
    return k or 0


def place(f, s):
    """src/gfx.c cv_text_flags: [(ch, gx, entry)] for a string drawn at x = 0"""
    out, pen, prev, psh, err = [], 0, 0, f["psh"], 0
    for ch in s:
        c = ord(ch)
        if ord("a") <= c <= ord("z") and f["last"] < ord("a"):
            c -= 32
        if prev:
            pen += f["kern"].get((prev << 8) | c, 0)
        gi = glyph(f, c)
        g0 = f["g"][gi << psh]
        if RULE[0] == "rounded":                     # the tables before the phases: the rounded pen
            pos = (pen + 8) & ~15
            e = g0
        elif psh and f["g"][(gi << psh) + 1][0] != g0[0]:   # the phase position nearer the previous glyph's offset
            step = 16 >> psh
            lo = pen - (pen & (step - 1))
            pos = lo if abs(lo - pen - err) <= abs(lo + step - pen - err) else lo + step
            e = f["g"][(gi << psh) + ((pos >> (4 - psh)) & ((1 << psh) - 1))]
        else:                                        # one phase: the rounded pen
            pos = (pen + 8) & ~15
            e = g0
        err = pos - pen
        out.append((ch, (pos >> 4) + e[2], e))
        pen += e[1]
        prev = c
    return out


def bitmap(f, e):
    off, adv, bx, by, bw, bh = e
    if f["hc"]:                                      # Huffman-coded (gen_aa_font.py huff_pack)
        return gf.huff_unpack(f["hc"], f["data"][off:], bw * bh)
    v = []
    for b in f["data"][off:off + (bw * bh + 1) // 2]:
        v += [b >> 4, b & 15]
    return v[:bw * bh]


def centroid(cols):
    m = sum(cols)
    return sum((i + 0.5) * c for i, c in enumerate(cols)) / m if m else None


def measure(f, font_hi, s):
    """[(pair, gap error px)] of one string"""
    x0 = 4
    placed = place(f, s)
    d = []
    for i, (ch, gx, e) in enumerate(placed):
        if ch == " " or not e[4]:
            d.append(None)
            continue
        pen = (font_hi.getlength(s[:i + 1]) - font_hi.getlength(s[i])) / SS
        w = int(pen + font_hi.getlength(s[i]) / SS) + 3 * f["h"]
        img = ar.render_cell(font_hi, ch, (w * SS, 3 * f["h"] * SS), round((x0 + pen) * SS), 2 * f["h"] * SS)
        a = img.reduce(SS)                               # box-filtered coverage on the pixel grid
        v = a.tobytes()
        ox = (x0 + pen) * SS                             # drawn at a whole 1/16 px: move the centroid the rest
        ref = centroid([sum(v[x::w]) for x in range(w)]) + (ox - round(ox)) / SS
        bm, bw = bitmap(f, e), e[4]
        cols = [sum(bm[y * bw + x] for y in range(e[5])) for x in range(bw)]
        d.append(x0 + gx + centroid(cols) - ref)
    return [(s[i:i + 2], d[i + 1] - d[i]) for i in range(len(s) - 1) if d[i] is not None and d[i + 1] is not None]


def specimen(f, words, scale=4):
    """the strings drawn by the table rule, gray on black, x scale"""
    lh = f["h"] + 4
    ws = [place(f, s) for s in words]
    w = max(gx + e[4] for p in ws for _, gx, e in p) + 8
    img = Image.new("L", (w, lh * len(words) + 4), 0)
    px = img.load()
    for k, p in enumerate(ws):
        for _, gx, e in p:
            bm = bitmap(f, e)
            for y in range(e[5]):
                for x in range(e[4]):
                    v = bm[y * e[4] + x] * 17
                    if v:
                        X, Y = 4 + gx + x, 2 + k * lh + e[3] + y
                        px[X, Y] = max(px[X, Y], v)
    return img.resize((img.width * scale, img.height * scale), Image.NEAREST)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("header", nargs="?", default="build/gen/ui_fonts.h")
    ap.add_argument("--png", help="directory for the specimen PNGs")
    ap.add_argument("--tag", default="after")
    ap.add_argument("--no-limit", action="store_true", help="report only")
    ap.add_argument("--rule", choices=["gfx", "rounded"], default="gfx",
                    help="placement: cv_text's (default) or the earlier rounded pen (to measure old tables)")
    a = ap.parse_args()
    RULE[0] = a.rule
    faces = parse(a.header)
    spec = {n: (font, px) for n, font, px, *_ in gf.preset("inter-tight")}
    bad = 0
    for name, f in faces.items():
        font, px = spec[name]
        font_hi = ar.open_font(font, px * SS)
        words = LOWER if f["last"] >= ord("z") else UPPER
        # a sparse face (L): the words it can draw (the firmware draws no others in it)
        words = [w for w in words if all(glyph_at(f, ord(ch.upper())) is not None for ch in w)]
        pairs = [p for s in words for p in measure(f, font_hi, s)]
        e = sorted(pairs, key=lambda p: -abs(p[1]))
        mx = abs(e[0][1])
        rms = (sum(v * v for _, v in pairs) / len(pairs)) ** 0.5
        over = sum(abs(v) > LIMIT[name] for _, v in pairs)
        ok = mx <= LIMIT[name] + 1e-9
        bad += not ok
        print(f"  {name} {px}px x{1 << f['psh']} phases: {len(pairs)} pairs, gap error max {mx:.3f} px, rms {rms:.3f} px, "
              f"{over} over {LIMIT[name]} px {'ok' if ok else 'FAIL'}")
        print("    worst: " + ", ".join(f"{p!r} {v:+.2f}" for p, v in e[:10]))
        if a.png:
            Path(a.png).mkdir(parents=True, exist_ok=True)
            out = Path(a.png) / f"{a.tag}_{name}.png"
            specimen(f, words).save(out)
            print(f"    specimen {out}")
    if a.no_limit:
        return 0
    print("text spacing: " + ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
