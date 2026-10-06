#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""The FM-1 controls diagram of the README (1200 px wide, the style of the 1.0 feature sheet: dark
rounded cards, white Inter Tight text, Fukiai line icons): a photo of the panel with every control
labelled by its Felucca function in small cards, leader lines ending in a dot on the control, and
cards for the hold actions.

  gen_panel_diagram.py PHOTO.jpg OUT.svg [--png OUT.png] [--jpg OUT.jpg]

PHOTO is the 1750 x 1050 photo of the panel the earlier diagram used; the control positions below
are in its pixels. The text is written as outlines (Inter Tight, assets/fonts,
OFL-1.1; icons from web/fukiai.ttf, MIT), so the SVG needs no font. --png / --jpg render it with
rsvg-convert. Checked: every dot inside its control's box on the photo, every text inside its card,
no two texts, cards or leader lines overlapping or crossing.
"""
import argparse
import base64
import io
import logging
import subprocess
import sys
from pathlib import Path

from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

logging.getLogger("fontTools").setLevel(logging.ERROR)   # fukiai.ttf: a harmless post-table note
ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "assets/fonts/InterTight[wght].ttf"
ICONS = ROOT / "web/fukiai.ttf"

W, H = 1200, 838
BG, CARD = "#1e1e1e", "#484848"                         # sampled from the feature sheet
R = 17                                                  # card corner radius (feature sheet)
DIM = 0.62                                              # opacity of secondary text

# --------------------------------------------------------------------------------------------- text
class Face:
    def __init__(self, font, tag):
        self.font = font
        self.tag = tag
        self.upm = font["head"].unitsPerEm
        self.cmap = font.getBestCmap()
        self.gs = font.getGlyphSet()
        self.hmtx = font["hmtx"]
        self.ids = {}

    def glyph(self, name):
        """the id of the glyph's path in <defs> (None: an empty glyph)"""
        if name not in self.ids:
            pen = SVGPathPen(self.gs)
            self.gs[name].draw(pen)
            d = pen.getCommands()
            self.ids[name] = f"{self.tag}{len(self.ids)}" if d else None
            if d:
                defs.append(f'<path id="{self.ids[name]}" d="{d}"/>')
        return self.ids[name]

    def bounds(self, name):
        pen = BoundsPen(self.gs)
        self.gs[name].draw(pen)
        return pen.bounds                       # font units, y up; None when empty

    def width(self, s, size, track=0.0):
        n = sum(self.hmtx[self.cmap[ord(c)]][0] for c in s)
        return n * size / self.upm + track * size * max(len(s) - 1, 0)


defs = []         # glyph outlines, once each
out = []          # svg elements
boxes = []        # (x0, y0, x1, y1, label, container)
desc = []         # every text, for <desc>


TEXT = {w: Face(instancer.instantiateVariableFont(TTFont(FONT), {"wght": w}), f"t{w}_") for w in (450, 600)}
ICON = Face(TTFont(ICONS), "i")


def ink(face, items, k, ox, oy):
    """the ink box of glyphs (name, pen_x in font units) drawn at (ox, oy) with scale k, y down"""
    xs, ys = [], []
    for name, px in items:
        b = face.bounds(name)
        if b:
            xs += [ox + (px + b[0]) * k, ox + (px + b[2]) * k]
            ys += [oy - b[3] * k, oy - b[1] * k]
    return min(xs), min(ys), max(xs), max(ys)


def text(s, x, y, size, weight=450, anchor="start", op=1.0, track=0.0, inside=None):
    """s at baseline y; anchor start / middle / end. inside = (x0, y0, x1, y1) it must fit in."""
    f = TEXT[weight]
    w = f.width(s, size, track)
    x0 = x - (w / 2 if anchor == "middle" else w if anchor == "end" else 0)
    k = size / f.upm
    parts, items, pen_x = [], [], 0.0
    for c in s:
        g = f.cmap[ord(c)]
        gid = f.glyph(g)
        if gid:
            parts.append(f'<use href="#{gid}" x="{pen_x:.0f}"/>')
            items.append((g, pen_x))
        pen_x += f.hmtx[g][0] + track * f.upm
    o = f' fill-opacity="{op}"' if op < 1 else ""
    out.append(f'<g transform="translate({x0:.2f} {y:.2f}) scale({k:.5f} {-k:.5f})"{o}>{"".join(parts)}</g>')
    ix0, iy0, ix1, iy1 = ink(f, items, k, x0, y)
    # the line box (cap height and descenders) or the ink, whichever is larger
    boxes.append((min(x0, ix0), min(y - size * 0.74, iy0), max(x0 + w, ix1), max(y + size * 0.22, iy1), s, inside))
    desc.append(s)
    return w


def icon(name, x, y, size, op=1.0, inside=None):
    """Fukiai glyph name, its em box with the top-left at (x, y)"""
    k = size / ICON.upm
    asc = ICON.font["hhea"].ascent
    o = f' fill-opacity="{op}"' if op < 1 else ""
    out.append(f'<use href="#{ICON.glyph(name)}" transform="translate({x:.2f} {y + asc * k:.2f}) '
               f'scale({k:.5f} {-k:.5f})"{o}/>')
    ix0, iy0, ix1, iy1 = ink(ICON, [(name, 0)], k, x, y + asc * k)
    boxes.append((min(x, ix0), min(y, iy0), max(x + size, ix1), max(y + size, iy1), name, inside))


def rect(x, y, w, h, r, fill="none", stroke=None, sw=1.5, op=1.0):
    s = f' stroke="#fff" stroke-width="{sw}" stroke-opacity="{op}"' if stroke else ""
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}"{s}/>')
    return (x, y, x + w, y + h)


def card(x0, y0, x1, y1, title, ico):
    rect(x0, y0, x1 - x0, y1 - y0, R, fill=CARD)
    box = (x0, y0, x1, y1)
    size = 21
    tw = TEXT[600].width(title, size)
    gap, isz = 9, 22
    left = (x0 + x1) / 2 - (tw + gap + isz) / 2
    icon(ico, left, y0 + 15, isz, inside=box)
    text(title, left + isz + gap, y0 + 34, size, 600, inside=box)
    return box


# --------------------------------------------------------------------------------------------- photo
# the photo's crop (inside the device body) and where it goes on the canvas
CROP = (70, 60, 1680, 960)
PX, PY, PW = 205, 112, 760
PS = PW / (CROP[2] - CROP[0])
PH = (CROP[3] - CROP[1]) * PS
PHOTO = (PX, PY, PX + PW, PY + PH)

# control: (anchor x, y on the photo, the control's box on the photo). The anchors of the knobs, OCT- and
# the keys are the label anchors of the earlier diagram; the buttons, OCT+ and the boxes are
# measured on the photo.
BTN_X = (997, 1105, 1207, 1315, 1417, 1525)
CTRL = {
    "MASTER": (175, 178, (125, 130, 225, 230)), "SELECT": (355, 178, (295, 130, 400, 230)),
    "PRESETS": (175, 350, (120, 305, 225, 400)), "ALGORITHM": (355, 350, (290, 305, 400, 400)),
    "OCT-": (198, 487, (152, 460, 240, 505)), "OCT+": (328, 487, (284, 460, 372, 505)),
    "KEYS": (870, 850, (90, 570, 1620, 915)),
}
for i, x in enumerate((978, 1163, 1348, 1533)):
    CTRL[f"KNOB {i + 1}"] = (x, 178, (x - 45, 130, x + 45, 225))
for row, y, names in ((0, 352, "FX SCL ENV LFO EDIT GLO"), (1, 454, "HOME SAVE ARP SEQ PLAY REC")):
    for x, n in zip(BTN_X, names.split()):
        CTRL[n] = (x, y, (x - 33, y - 28, x + 33, y + 30))


def at(name):
    """the canvas point of a control's anchor"""
    x, y, _ = CTRL[name]
    return PX + (x - CROP[0]) * PS, PY + (y - CROP[1]) * PS


def photo_y(y):
    return PY + (y - CROP[1]) * PS


def photo(path):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    assert im.size == (1750, 1050), im.size
    im = im.crop(CROP).resize((PW * 2, round(PH * 2)), Image.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, "JPEG", quality=86, optimize=True)
    data = base64.b64encode(buf.getvalue()).decode()
    defs.append(f'<clipPath id="pc"><rect x="{PX}" y="{PY}" width="{PW}" height="{PH:.2f}" rx="{R}"/></clipPath>')
    out.append(f'<rect x="{PX}" y="{PY}" width="{PW}" height="{PH:.2f}" rx="{R}" fill="{BG}"/>')
    out.append(f'<image x="{PX}" y="{PY}" width="{PW}" height="{PH:.2f}" clip-path="url(#pc)" '
               f'preserveAspectRatio="none" href="data:image/jpeg;base64,{data}"/>')


# --------------------------------------------------------------------------------------------- labels
solids = []       # cards and the photo: (x0, y0, x1, y1, name), none may overlap
lines = []        # leader segments ((x0, y0), (x1, y1), name), none may cross another leader
dots = []


def label(x0, y0, w, h, printed, func, hold=None):
    """a small card: the printed name (small caps), the function, the hold action (clock icon)"""
    rect(x0, y0, w, h, 10, fill=CARD)
    box = (x0, y0, x0 + w, y0 + h)
    solids.append(box + (printed,))
    text(printed, x0 + 12, y0 + 18, 11, 600, op=DIM, track=0.08, inside=box)
    text(func, x0 + 12, y0 + 37, 14.5, 600, inside=box)
    if hold:
        icon("symbol_stopwatch", x0 + 12, y0 + 43, 12, op=DIM, inside=box)
        text(hold, x0 + 28, y0 + 53.5, 12.5, 450, op=DIM, inside=box)
    return box


def leader(name, *pts):
    """from the control's dot through pts (canvas points); the last one is on a card's edge"""
    p = [at(name)] + list(pts)
    for a, b in zip(p, p[1:]):
        lines.append((a, b, name))
    d = " ".join(f"{x:.1f},{y:.1f}" for x, y in p)
    out.append(f'<polyline points="{d}" fill="none" stroke="{BG}" stroke-opacity="0.75" stroke-width="3.6" '
               f'stroke-linejoin="round" stroke-linecap="round"/>')
    out.append(f'<polyline points="{d}" fill="none" stroke="#fff" stroke-width="1.4" stroke-linejoin="round"/>')
    dots.append((name, p[0]))


def draw_dots():
    for _, (x, y) in dots:
        out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.2" fill="#fff" stroke="{BG}" stroke-width="2"/>')


def build(photo_path):
    out.append(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
    title = "FM-1 controls in Felucca 1.0"
    tw = TEXT[600].width(title, 21)
    left = W / 2 - (tw + 31) / 2
    icon("ui_knob", left, 13, 22)
    text(title, left + 31, 32, 21, 600)
    photo(photo_path)

    LW, LH, GAP = 160, 62, 8
    # left: MASTER, PRESETS, ALGORITHM, OCT-, OCT+
    left_items = (("MASTER", "Volume", None, None), ("PRESETS", "Sound", None, None),
                  ("ALGORITHM", "Track T1–T4", None, 425), ("OCT-", "Octave −, back", None, None),
                  ("OCT+", "Octave +, do it", None, 538))
    step = (PH - LH) / (len(left_items) - 1)
    for i, (n, f, hold, lane) in enumerate(left_items):
        y0 = PY + i * step
        b = label(23, y0, LW, LH, n.replace("-", "−"), f, hold)
        ax, ay = at(n)
        pts = []
        if lane:
            pts += [(ax, photo_y(lane)), (PX + 8, photo_y(lane))]
        else:
            pts += [(PX + 8, ay)]
        pts.append((b[2], (b[1] + b[3]) / 2))
        leader(n, *pts)

    # top: SELECT, KNOB 1-4
    b = label(285, 50, 150, 54, "SELECT", "BPM")
    leader("SELECT", (at("SELECT")[0], b[3]))
    k1, k4 = at("KNOB 1")[0], at("KNOB 4")[0]
    b = rect(k1 - 42, 50, k4 - k1 + 84, 54, 10, fill=CARD)
    solids.append(b + ("KNOB 1-4",))
    text("KNOB 1 – 4", b[0] + 12, 68, 11, 600, op=DIM, track=0.08, inside=b)
    text("The four columns of the page", b[0] + 12, 87, 14.5, 600, inside=b)
    for i in range(4):
        x = at(f"KNOB {i + 1}")[0]
        leader(f"KNOB {i + 1}", (x, b[3]))
    # the hold legend, top right
    icon("symbol_stopwatch", 990, 70, 14, op=0.8)
    text("what a button does held", 1009, 82, 13.5, 450, op=0.8)

    # right: the top row of buttons, lanes above the row (FX the highest, so no leader crosses)
    top = (("FX", "Effects", "FX layer"), ("SCL", "Scale, chords", "SCL layer"), ("ENV", "Envelope", None),
           ("LFO", "LFO, mod matrix", None), ("EDIT", "Engine", "EDIT layer"), ("GLO", "Mixer, global", "GLO layer"))
    RX, RW = 987, 190
    step = (PH - LH) / 5
    for i, (n, f, hold) in enumerate(top):
        y0 = PY + i * step
        b = label(RX, y0, RW, LH, n, f, hold)
        ax, ay = at(n)
        lane = photo_y(250 + i * 12)
        leader(n, (ax, lane), (PX + PW - 8, lane), (b[0], (b[1] + b[3]) / 2))

    # bottom: the keys and the bottom row of buttons, straight down out of the photo, then to their card
    bottom = (("KEYS", "Keys F3 – G5", "play the track"), ("HOME", "Home", "menu"), ("SAVE", "Save, load", "undo"),
              ("ARP", "Arpeggiator", None), ("SEQ", "Sequencer", "song"), ("PLAY", "Play, stop", None),
              ("REC", "Record", None))
    BW, BY = 102, PY + PH + 24
    gap = (PW + 20 - len(bottom) * BW) / (len(bottom) - 1)
    for i, (n, f, hold) in enumerate(bottom):
        x0 = PX - 20 + i * (BW + gap)
        if n == "KEYS":
            rect(x0, BY, BW, LH, 10, fill=CARD)
            b = (x0, BY, x0 + BW, BY + LH)
            solids.append(b + (n,))
            text("KEYS", x0 + 12, BY + 18, 11, 600, op=DIM, track=0.08, inside=b)
            text("F3 – G5", x0 + 12, BY + 37, 14.5, 600, inside=b)
            text(hold, x0 + 12, BY + 53.5, 12.5, 450, op=DIM, inside=b)
        else:
            b = label(x0, BY, BW, LH, n, f, hold)
        ax, ay = at(n)
        leader(n, (ax, PY + PH - 6), ((b[0] + b[2]) / 2, b[1]))
    draw_dots()

    # the summary cards
    def rows_of(box, items, name_w, y0, step=24, size=15):
        y = y0
        for name, lns in items:
            text(name, box[0] + 26, y, size, 600, inside=box)
            for ln in lns:
                text(ln, box[0] + 26 + name_w, y, size, 450, op=0.85, inside=box)
                y += step - 4
            y += 4 + (step - 20)

    CY = BY + LH + 16
    B = card(23, CY, 527, H - 9, "Hold for a quick layer", "control_fx")
    rows_of(B, (("FX", ("repeat, reverse, filter sweeps, tape stop, freeze,",
                        "harmonizer; mutes on the black keys")),
                ("GLO", ("mute, solo, tap tempo; KNOB 1–4 track levels",)),
                ("SCL", ("a key sets the root; scale, chord keys",)),
                ("EDIT", ("a white key picks the engine",))), 58, CY + 65)
    C = card(541, CY, 845, H - 9, "Hold a button", "symbol_stopwatch")
    rows_of(C, (("SAVE", ("undo, and redo",)), ("HOME", ("the menu",)), ("SEQ", ("SONG: chain patterns",))),
            70, CY + 73, step=30)
    D = card(859, CY, 1177, H - 9, "OCT+ and OCT−", "control_arrow_up_down")
    y = CY + 73
    for ln, op in (("Octave up and down, both: reset", 0.85),
                   ("On action pages, dialogs, the menu:", 0.85),
                   ("OCT+ does it, OCT− goes back", 1.0)):
        text(ln, (D[0] + D[2]) / 2, y, 15, 600 if op == 1.0 else 450, "middle", op=op, inside=D)
        y += 30
    solids.extend([B + ("quick layers",), C + ("hold",), D + ("oct",), PHOTO + ("photo",)])


def cross(a, b, c, d):
    """segments ab and cd properly cross (touching at an end does not count)"""
    def o(p, q, r):
        v = (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
        return 0 if abs(v) < 1e-6 else (1 if v > 0 else -1)
    return o(a, b, c) * o(a, b, d) < 0 and o(c, d, a) * o(c, d, b) < 0


def check():
    bad = []
    for name, (x, y) in dots:                       # the dot is on its control
        cx, cy, (x0, y0, x1, y1) = CTRL[name]
        if not (x0 <= cx <= x1 and y0 <= cy <= y1):
            bad.append(f"anchor of {name} off its control box")
        if not (PHOTO[0] + 4 <= x <= PHOTO[2] - 4 and PHOTO[1] + 4 <= y <= PHOTO[3] - 4):
            bad.append(f"dot of {name} off the photo: {x:.0f},{y:.0f}")
        if abs(x - (PX + (cx - CROP[0]) * PS)) > 0.01 or abs(y - (PY + (cy - CROP[1]) * PS)) > 0.01:
            bad.append(f"dot of {name} not at its anchor")
        for other, (ox, oy, ob) in CTRL.items():   # and on no other control
            if other != name and other != "KEYS" and name != "KEYS" and ob[0] <= cx <= ob[2] and ob[1] <= cy <= ob[3]:
                bad.append(f"anchor of {name} inside {other}")
    for b in boxes:
        x0, y0, x1, y1, s, c = b
        if not (0 <= x0 and x1 <= W and 0 <= y0 and y1 <= H):
            bad.append(f"off the canvas: {s!r}")
        if c and not (c[0] + 6 <= x0 and x1 <= c[2] - 6 and c[1] + 4 <= y0 and y1 <= c[3] - 4):
            bad.append(f"outside its box: {s!r} {tuple(round(v) for v in b[:4])} in {c}")
    for i, a in enumerate(boxes):
        for b in boxes[i + 1:]:
            if a[0] < b[2] - 0.5 and b[0] < a[2] - 0.5 and a[1] < b[3] - 0.5 and b[1] < a[3] - 0.5:
                bad.append(f"overlap: {a[4]!r} / {b[4]!r}")
    for i, a in enumerate(solids):
        if not (0 <= a[0] and a[2] <= W and 0 <= a[1] and a[3] <= H):
            bad.append(f"card off the canvas: {a[4]}")
        for b in solids[i + 1:]:
            if a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]:
                bad.append(f"cards overlap: {a[4]} / {b[4]}")
    for i, (a0, a1, an) in enumerate(lines):
        for b0, b1, bn in lines[i + 1:]:
            if an != bn and cross(a0, a1, b0, b1):
                bad.append(f"leaders cross: {an} / {bn}")
        for t in boxes:                               # no leader through a text
            if seg_hits_box(a0, a1, t[:4]):
                bad.append(f"leader of {an} through {t[4]!r}")
        for other, (_, _, ob) in CTRL.items():       # nor over another control (the keys excepted)
            if other not in (an, "KEYS"):
                cb = (PX + (ob[0] - CROP[0]) * PS, PY + (ob[1] - CROP[1]) * PS,
                      PX + (ob[2] - CROP[0]) * PS, PY + (ob[3] - CROP[1]) * PS)
                if seg_hits_box(a0, a1, cb):
                    bad.append(f"leader of {an} over {other}")
    return bad


def seg_hits_box(a, b, box):
    x0, y0, x1, y1 = box
    for i in range(41):
        t = i / 40
        x, y = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
        if x0 < x < x1 and y0 < y < y1:
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("photo")
    ap.add_argument("svg")
    ap.add_argument("--png")
    ap.add_argument("--jpg")
    a = ap.parse_args()
    build(a.photo)
    bad = check()
    if bad:
        print("\n".join(bad), file=sys.stderr)
        sys.exit(1)
    d = " / ".join(desc).replace("&", "&amp;").replace("<", "&lt;")
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" fill="#fff">'
           f'<title>FM-1 controls in Felucca 1.0</title><desc>{d}</desc>\n'
           f'<defs>{"".join(defs)}</defs>\n' + "\n".join(out) + "\n</svg>\n")
    Path(a.svg).write_text(svg, encoding="utf-8")
    print(f"{a.svg}: {W} x {H}, {len(dots)} dots, {len(boxes)} texts, {len(solids)} cards, "
          f"{len(lines)} leader segments checked, {len(svg)} bytes")
    for out_path in (a.png, a.jpg):
        if not out_path:
            continue
        png = out_path if out_path == a.png else out_path + ".tmp.png"
        subprocess.run(["rsvg-convert", "-w", str(W), "-h", str(H), "-o", png, a.svg], check=True)
        if out_path == a.jpg:
            from PIL import Image
            Image.open(png).convert("RGB").save(out_path, "JPEG", quality=90, optimize=True, progressive=True,
                                                 subsampling=0)
            Path(png).unlink()
        print(f"{out_path}: {Path(out_path).stat().st_size} bytes")


if __name__ == "__main__":
    main()
