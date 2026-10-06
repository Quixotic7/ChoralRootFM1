#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Pre-rasterise TTF fonts into anti-aliased 4-bit alpha glyph tables for the UI (run by
tools/build.py generate() with --preset inter-tight).

  gen_aa_font.py OUT.h --preset standin|inter-tight [--tracking EM] [--gamma G] [--kern-min N]
  gen_aa_font.py OUT.h --face NAME=FONT[#index]:PX:FIRST-LAST[+0xNN,...] ...   (explicit faces)

Faces of the presets (S labels, M values/headers, L big numerals / titles):
  standin     an installed Inter (~/Library/Fonts/Inter.ttc, else Inter in fonts/ next to tools/) with
              -2.5 % tracking: a stand-in for Inter Tight, for previews only.
  inter-tight assets/fonts/InterTight[wght].ttf (SIL OFL 1.1, Google Fonts) at weights 400 / 500 / 600.
  choralroot  the same font at weight 700: CRX 104 px (the chord charset) and CRB 40 px (ASCII), one phase each,
              for ChoralRoot's big type (build/gen/cr_fonts.h; firmware/src/cr_gfx.c draws them at any size below)

Output (a C header; the glyph / font types live in the renderer, firmware/src/gfx.c):
  AF_<N>_DATA   4-bit alpha, trimmed to the ink box, rows back to back, each glyph a byte-aligned bit stream
                (MSB first) of canonical Huffman codes (AF_<N>_HC); a symbol is a value and a repeat count:
                a run of 1..16 zeros or 15s, or one value 1..14 (huff_pack; about 28 % less than 2 px per byte)
  AF_<N>_HC     the code: the number of codes of each length 1..15, then the symbols in code order
                ((count - 1) << 4 | value)
  AF_<N>_G      aag_t {off, adv (1/16 px), bx, by, bw, bh} per glyph and phase (PHASES of S M, PHASES_L of L:
                the glyph drawn at a pen of +0, +1/phases .. px, next to each other), ASCII 32..126, then the
                extras (Latin-1 by their code; the ellipsis U+2026 as code 0x85, as in Windows-1252)
  AF_<N>_KERN / _KD  kerning pairs (key = a<<8|b, sorted) and their deltas in 1/16 px (GPOS of the weight)
  AF_<N>        aafont_t (its last field: log2 of the phases)
Prints the byte cost of every face.
"""
import argparse
import heapq
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aa_raster as ar  # noqa: E402

FONTS = Path(__file__).resolve().parents[1] / "fonts"
LOCAL_INTER = os.path.expanduser("~/Library/Fonts/Inter.ttc")   # 4.001: 0 Regular, 10 Medium, 12 SemiBold
EXTRAS = [0xA9, 0xB0, 0xB7, 0xC4, 0xD6, 0xDC, 0xE4, 0xF6, 0xFC, 0x2026]  # (c) deg . A: O: U: a: o: u: (Latin-1), ...
CODE = {0x2026: 0x85}                  # characters outside Latin-1 -> their one-byte code in the firmware
PHASES, PHASES_L = 4, 2                # horizontal phases per glyph (aa_raster.raster_font): S M, L
# faces stored Huffman-coded (--huff): M and L. S, the most drawn and the least compressible (-15 %), stays
# 2 px per byte, so the labels draw at full speed
HUFF = [("M", "L")]
# L draws only "FELUCCA", the UPDATE MODE countdown digit and the calibration's control names (panel.c
# B_NAME / E_NAME): a sparse face of those glyphs, the space as its range and the rest as extras
# (ui_test.c checks that every L string is covered)
L_CHARS = " +-0123456789ABCDEFGHIKLMNOPQRSTUVXY"
# ChoralRoot FM-1 (firmware/src/cr_draw.c, CR_SCREENS.md): two big faces at weight 700, one phase each (cr_gfx.c
# places and resamples them itself: the chord name's squeeze, any size below the face's):
#   CRX 104 px  the chord name's root / quality and the meters' numbers: the chord charset (roots, # b, the
#               qualities m dim sus +, extensions M7 9 JAZZ ..), figures, and PANIC's letters; sparse like L
#   CRB 40 px   everything else bold: pickers, values, the superscript, the notes line, titles (full ASCII)
CRX_CHARS = " #+-.0123456789?ABCDEFGIJMNPTWZbdimsu"
HUFF_CR = ("CRX", "CRB")
PROPORTIONAL = ("CRX", "CRB")          # proportional figures (the mock-ups' "13", "120"), not tabular


def preset(name):
    if name == "inter-tight":
        v = str(FONTS.parent / "assets" / "fonts" / "InterTight[wght].ttf")   # SIL OFL 1.1, Google Fonts
        return [("S", v + "@400", 12, (32, 126), EXTRAS, 0.0, PHASES),
                ("M", v + "@500", 15, (32, 126), EXTRAS, 0.0, PHASES),
                ("L", v + "@600", 28, (32, 32), [ord(c) for c in L_CHARS[1:]], 0.0, PHASES_L)]
    if name == "choralroot":
        v = str(FONTS.parent / "assets" / "fonts" / "InterTight[wght].ttf")
        return [("CRX", v + "@700", 104, (32, 32), [ord(c) for c in CRX_CHARS[1:]], 0.0, 1),
                ("CRB", v + "@700", 40, (32, 126), EXTRAS, 0.0, 1)]
    if name == "standin":
        if os.path.exists(LOCAL_INTER):
            reg, med, semi = LOCAL_INTER + "#0", LOCAL_INTER + "#10", LOCAL_INTER + "#12"
        else:                                  # the repo's Inter 3.019 (only Regular + Light)
            reg = med = semi = str(FONTS / "Inter-Regular.ttf")
        t = -0.025                             # Inter Tight is Inter with tighter spacing
        return [("S", reg, 12, (32, 126), EXTRAS, t, PHASES), ("M", med, 15, (32, 126), EXTRAS, t, PHASES),
                ("L", semi, 28, (32, 32), [ord(c) for c in L_CHARS[1:]], t, PHASES_L)]
    raise SystemExit(f"unknown preset {name}")


def parse_face(s):
    name, _, rest = s.partition("=")
    font, px, rng = rest.rsplit(":", 2)
    extras = []
    if "+" in rng:
        rng, ex = rng.split("+", 1)
        extras = [int(x, 0) for x in ex.split(",") if x]
    a, b = rng.split("-")
    return (name, font, int(px), (int(a), int(b)), extras, None, PHASES)


HC_MAXLEN = 15


def huff_symbols(v):
    """a glyph's alpha values -> symbols (count - 1) << 4 | value: runs of 1..16 zeros or 15s, single others"""
    out, i = [], 0
    while i < len(v):
        j = i + 1
        if v[i] in (0, 15):
            while j < len(v) and v[j] == v[i] and j - i < 16:
                j += 1
        out.append((j - i - 1) << 4 | v[i])
        i = j
    return out


def huff_lengths(count):
    """symbol -> code length (Huffman), at most HC_MAXLEN"""
    heap = [(c, k, (s,)) for k, (s, c) in enumerate(sorted(count.items()))]
    heapq.heapify(heap)
    length = {s: 0 for s in count}
    if len(heap) == 1:
        return {s: 1 for s in count}
    k = len(heap)
    while len(heap) > 1:
        a, b = heapq.heappop(heap), heapq.heappop(heap)
        for s in a[2] + b[2]:
            length[s] += 1
        k += 1
        heapq.heappush(heap, (a[0] + b[0], k, a[2] + b[2]))
    assert max(length.values()) <= HC_MAXLEN, "code too long: limit the lengths"
    return length


def huff_pack(glyph_values):
    """[alpha values per glyph] -> (hc table bytes, [bytes per glyph]): canonical Huffman, MSB first"""
    syms = [huff_symbols(v) for v in glyph_values]
    count = {}
    for g in syms:
        for s in g:
            count[s] = count.get(s, 0) + 1
    length = huff_lengths(count)
    order = sorted(count, key=lambda s: (length[s], s))
    code, c, prev = {}, 0, length[order[0]]
    for s in order:                                    # canonical: by length, then symbol
        c <<= length[s] - prev
        prev = length[s]
        code[s] = c
        c += 1
    hc = [sum(1 for s in order if length[s] == n) for n in range(1, HC_MAXLEN + 1)] + order
    out = []
    for g in syms:
        acc, nb, b = 0, 0, bytearray()
        for s in g:
            acc, nb = (acc << length[s]) | code[s], nb + length[s]
            while nb >= 8:
                nb -= 8
                b.append((acc >> nb) & 255)
        if nb:
            b.append((acc << (8 - nb)) & 255)
        out.append(bytes(b))
    return hc, out


def huff_unpack(hc, data, n):
    """the decoder (gfx.c cv_alpha_hc), for the self-check: n values from the bit stream data"""
    out, pos = [], 0
    while len(out) < n:
        code = first = idx = 0
        for ln in range(HC_MAXLEN):
            code |= (data[pos >> 3] >> (7 - (pos & 7))) & 1
            pos += 1
            if code - first < hc[ln]:
                break
            idx += hc[ln]
            first = (first + hc[ln]) << 1
            code <<= 1
        s = hc[HC_MAXLEN + idx + code - first]
        out += [s & 15] * ((s >> 4) + 1)
    assert len(out) == n
    return out


def emit(face, tracking, gamma, kern_min):
    name, font, px, (first, last), extras, ftrack, phases = face
    chars = [chr(c) for c in range(first, last + 1)] + [chr(c) for c in extras]
    r = ar.raster_font(font, px, chars, tracking if ftrack is None else ftrack, gamma, name not in PROPORTIONAL,
                       kern_min, phases)
    data, table, lines = bytearray(), [], []
    for ch in chars:
        adv16, ph = r["glyphs"][ch]
        off = len(data)
        for bx, by, bw, bh, b in ph:
            table.append((len(data), adv16, bx, by, bw, bh))
            data += b
        table += [(off,) + table[-1][1:]] * (phases - len(ph))   # one phase (aa_raster.FIXED): the same bitmap
    raw, hc = bytes(data), []
    if name in HUFF[0]:
        # Huffman-code every distinct bitmap (phases may share one), byte-aligned per glyph
        offs = sorted({e[0] for e in table if e[4]})
        vals = []
        for off in offs:
            e = next(t for t in table if t[0] == off and t[4])
            v = []
            for byte in raw[off:off + (e[4] * e[5] + 1) // 2]:
                v += [byte >> 4, byte & 15]
            vals.append(v[:e[4] * e[5]])
        hc, packed = huff_pack(vals)
        data, newoff = bytearray(), {}
        for off, v, b in zip(offs, vals, packed):
            assert huff_unpack(hc, b, len(v)) == v
            newoff[off] = len(data)
            data += b
        table = [(newoff.get(off, 0),) + t[1:] for t in table for off in (t[0],)]
        data += bytes(2)                               # gfx.c cv_alpha_hc reads up to 2 bytes ahead
    assert len(data) < 65536, "glyph data over 64 KiB: split the face"
    kern = sorted(r["kern"].items())
    keys = [(ord(a) << 8) | ord(b) for (a, b), _ in kern if ord(a) < 256 and ord(b) < 256]
    deltas = [d for (a, b), d in kern if ord(a) < 256 and ord(b) < 256]
    order = sorted(range(len(keys)), key=lambda i: keys[i])
    keys, deltas = [keys[i] for i in order], [deltas[i] for i in order]
    psh = phases.bit_length() - 1
    lines.append(f"/* {name}: {os.path.basename(font.split('#')[0])} {px}px, h {r['h']}, baseline {r['asc']}, "
                 f"{len(chars)} glyphs x {phases} phases, {len(data)} B data"
                 + (f" (Huffman; {len(raw)} B as nibbles) */" if hc else " */"))
    lines.append(ar.c_array(f"AF_{name}_DATA", "uint8_t", list(data), 24, "0x{:02x}"))
    if hc:
        lines.append(ar.c_array(f"AF_{name}_HC", "uint8_t", hc, 24))
    lines.append(f"static const aag_t AF_{name}_G[{len(table)}] = {{")
    for off, adv, bx, by, bw, bh in table:
        lines.append(f"    {{{off}, {adv}, {bx}, {by}, {bw}, {bh}}},")
    lines.append("};")
    if keys:
        lines.append(ar.c_array(f"AF_{name}_KERN", "uint16_t", keys, 12))
        lines.append(ar.c_array(f"AF_{name}_KD", "int8_t", deltas, 24))
    ex = ", ".join(str(CODE.get(c, c)) for c in extras) or "0"
    lines.append(f"static const uint8_t AF_{name}_EX[{max(1, len(extras))}] = {{{ex}}};")
    lines.append(f"static const aafont_t AF_{name} = {{{r['h']}, {r['asc']}, {first}, {last}, {len(extras)}, {len(keys)}, "
                 f"AF_{name}_G, AF_{name}_DATA, {'AF_%s_KERN' % name if keys else '0'}, "
                 f"{'AF_%s_KD' % name if keys else '0'}, AF_{name}_EX, {psh}, {'AF_%s_HC' % name if hc else '0'}}};")
    cost = {"data": len(data) + len(hc), "glyph_table": 8 * len(table), "kern": 3 * len(keys), "extras": len(extras),
            "glyphs": len(chars), "phases": phases, "pairs": len(keys), "h": r["h"], "asc": r["asc"], "px": px,
            "font": os.path.basename(font.split("#")[0]), "adv_digit": r["glyphs"]["0"][0] / 16}
    cost["total"] = cost["data"] + cost["glyph_table"] + cost["kern"] + cost["extras"]
    return "\n".join(lines) + "\n", cost


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--preset", choices=["standin", "inter-tight", "choralroot"])
    ap.add_argument("--face", action="append", default=[])
    ap.add_argument("--tracking", type=float, default=0.0, help="extra advance per glyph in em")
    ap.add_argument("--gamma", type=float, default=1.0, help="coverage curve; <1 thickens light-on-dark text")
    ap.add_argument("--kern-min", type=int, default=1, help="smallest kerning pair kept, in 1/16 px (0 pairs = no kerning)")
    ap.add_argument("--huff", default=None, help="faces stored Huffman-coded (comma list, '' = none; "
                    "default M,L, the choralroot preset CRX,CRB)")
    a = ap.parse_args()
    if a.huff is not None:
        HUFF[0] = tuple(x for x in a.huff.split(",") if x)
    elif a.preset == "choralroot":
        HUFF[0] = HUFF_CR
    faces = ([parse_face(s) for s in a.face] if a.face else []) or (preset(a.preset) if a.preset else None)
    if not faces:
        ap.error("--preset or --face is required")
    out = ["/* generated by tools/gen_aa_font.py: 4-bit alpha glyphs (Inter Tight, SIL OFL 1.1, "
           "Copyright 2022 The Inter Project Authors; see assets/fonts/OFL.txt) */",
           "#pragma once", "#include <stdint.h>", ""]
    total = 0
    for f in faces:
        text, c = emit(f, a.tracking, a.gamma, a.kern_min)
        out.append(text)
        total += c["total"]
        print(f"face {f[0]}: {c['font']} {c['px']}px h{c['h']} base{c['asc']}  {c['glyphs']} glyphs x {c['phases']}  "
              f"data {c['data']} B + table {c['glyph_table']} B + kern {c['kern']} B ({c['pairs']} pairs) "
              f"= {c['total']} B   digit adv {c['adv_digit']:.2f}px")
    Path(a.out).write_text("\n".join(out))
    print(f"fonts total {total} B  -> {a.out}")


if __name__ == "__main__":
    main()
